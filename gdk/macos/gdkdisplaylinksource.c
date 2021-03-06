/* gdkdisplaylinksource.c
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Christian Hergert <christian@hergert.me>
 */

#include "config.h"

#include <AppKit/AppKit.h>
#include <mach/mach_time.h>

#include "gdkdisplaylinksource.h"

#include "gdkmacoseventsource-private.h"

static gint64 host_to_frame_clock_time (gint64 val);

static gboolean
gdk_display_link_source_prepare (GSource *source,
                                 int     *timeout_)
{
  GdkDisplayLinkSource *impl = (GdkDisplayLinkSource *)source;
  gint64 now;

  now = g_source_get_time (source);

  if (now < impl->presentation_time)
    *timeout_ = (impl->presentation_time - now) / 1000L;
  else
    *timeout_ = -1;

  return impl->needs_dispatch;
}

static gboolean
gdk_display_link_source_check (GSource *source)
{
  GdkDisplayLinkSource *impl = (GdkDisplayLinkSource *)source;
  return impl->needs_dispatch;
}

static gboolean
gdk_display_link_source_dispatch (GSource     *source,
                                  GSourceFunc  callback,
                                  gpointer     user_data)
{
  GdkDisplayLinkSource *impl = (GdkDisplayLinkSource *)source;
  gboolean ret = G_SOURCE_CONTINUE;

  impl->needs_dispatch = FALSE;

  if (callback != NULL)
    ret = callback (user_data);

  return ret;
}

static void
gdk_display_link_source_finalize (GSource *source)
{
  GdkDisplayLinkSource *impl = (GdkDisplayLinkSource *)source;

  CVDisplayLinkStop (impl->display_link);
  CVDisplayLinkRelease (impl->display_link);
}

static GSourceFuncs gdk_display_link_source_funcs = {
  gdk_display_link_source_prepare,
  gdk_display_link_source_check,
  gdk_display_link_source_dispatch,
  gdk_display_link_source_finalize
};

void
gdk_display_link_source_pause (GdkDisplayLinkSource *source)
{
  CVDisplayLinkStop (source->display_link);
}

void
gdk_display_link_source_unpause (GdkDisplayLinkSource *source)
{
  CVDisplayLinkStart (source->display_link);
}

static CVReturn
gdk_display_link_source_frame_cb (CVDisplayLinkRef   display_link,
                                  const CVTimeStamp *inNow,
                                  const CVTimeStamp *inOutputTime,
                                  CVOptionFlags      flagsIn,
                                  CVOptionFlags     *flagsOut,
                                  void              *user_data)
{
  GdkDisplayLinkSource *impl = user_data;
  gint64 presentation_time;
  gboolean needs_wakeup;

  needs_wakeup = !g_atomic_int_get (&impl->needs_dispatch);

  presentation_time = host_to_frame_clock_time (inOutputTime->hostTime);

  impl->presentation_time = presentation_time;
  impl->needs_dispatch = TRUE;

  if (needs_wakeup)
    {
      NSEvent *event;

      /* Post a message so we'll break out of the message loop.
       *
       * We don't use g_main_context_wakeup() here because that
       * would result in sending a message to the pipe(2) fd in
       * the select thread which would then send this message as
       * well. Lots of extra work.
       */
      event = [NSEvent otherEventWithType: NSEventTypeApplicationDefined
                                 location: NSZeroPoint
                            modifierFlags: 0
                                timestamp: 0
                             windowNumber: 0
                                  context: nil
                                  subtype: GDK_MACOS_EVENT_SUBTYPE_EVENTLOOP
                                    data1: 0
                                    data2: 0];

      [NSApp postEvent:event atStart:YES];
    }

  return kCVReturnSuccess;
}

/**
 * gdk_display_link_source_new:
 *
 * Creates a new `GSource` that will activate the dispatch function upon
 * notification from a CVDisplayLink that a new frame should be drawn.
 *
 * Effort is made to keep the transition from the high-priority
 * CVDisplayLink thread into this GSource lightweight. However, this is
 * somewhat non-ideal since the best case would be to do the drawing
 * from the high-priority thread.
 *
 * Returns: (transfer full): A newly created `GSource`
 */
GSource *
gdk_display_link_source_new (void)
{
  GdkDisplayLinkSource *impl;
  GSource *source;
  CVReturn ret;
  double period;

  source = g_source_new (&gdk_display_link_source_funcs, sizeof *impl);
  impl = (GdkDisplayLinkSource *)source;

  /*
   * Create our link based on currently connected displays.
   * If there are multiple displays, this will be something that tries
   * to work for all of them. In the future, we may want to explore multiple
   * links based on the connected displays.
   */
  ret = CVDisplayLinkCreateWithActiveCGDisplays (&impl->display_link);
  if (ret != kCVReturnSuccess)
    {
      g_warning ("Failed to initialize CVDisplayLink!");
      return source;
    }

  /*
   * Determine our nominal period between frames.
   */
  period = CVDisplayLinkGetActualOutputVideoRefreshPeriod (impl->display_link);
  if (period == 0.0)
    period = 1.0 / 60.0;
  impl->refresh_interval = period * 1000000L;
  impl->refresh_rate = 1.0 / period * 1000L;

  /*
   * Wire up our callback to be executed within the high-priority thread.
   */
  CVDisplayLinkSetOutputCallback (impl->display_link,
                                  gdk_display_link_source_frame_cb,
                                  source);

  g_source_set_static_name (source, "[gdk] quartz frame clock");

  return source;
}

static gint64
host_to_frame_clock_time (gint64 val)
{
  /* NOTE: Code adapted from GLib's g_get_monotonic_time(). */

  mach_timebase_info_data_t timebase_info;

  /* we get nanoseconds from mach_absolute_time() using timebase_info */
  mach_timebase_info (&timebase_info);

  if (timebase_info.numer != timebase_info.denom)
    {
#ifdef HAVE_UINT128_T
      val = ((__uint128_t) val * (__uint128_t) timebase_info.numer) / timebase_info.denom / 1000;
#else
      guint64 t_high, t_low;
      guint64 result_high, result_low;

      /* 64 bit x 32 bit / 32 bit with 96-bit intermediate
       * algorithm lifted from qemu */
      t_low = (val & 0xffffffffLL) * (guint64) timebase_info.numer;
      t_high = (val >> 32) * (guint64) timebase_info.numer;
      t_high += (t_low >> 32);
      result_high = t_high / (guint64) timebase_info.denom;
      result_low = (((t_high % (guint64) timebase_info.denom) << 32) +
                    (t_low & 0xffffffff)) /
                   (guint64) timebase_info.denom;
      val = ((result_high << 32) | result_low) / 1000;
#endif
    }
  else
    {
      /* nanoseconds to microseconds */
      val = val / 1000;
    }

  return val;
}
