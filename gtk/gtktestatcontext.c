/* gtktestatcontext.c: Test AT context
 *
 * Copyright 2020  GNOME Foundation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gtktestatcontextprivate.h"

#include "gtkatcontextprivate.h"
#include "gtkdebug.h"
#include "gtkenums.h"
#include "gtkprivate.h"
#include "gtktypebuiltins.h"

struct _GtkTestATContext
{
  GtkATContext parent_instance;
};

struct _GtkTestATContextClass
{
  GtkATContextClass parent_class;
};

G_DEFINE_TYPE (GtkTestATContext, gtk_test_at_context, GTK_TYPE_AT_CONTEXT)

static void
gtk_test_at_context_state_change (GtkATContext                *self,
                                  GtkAccessibleStateChange     changed_states,
                                  GtkAccessiblePropertyChange  changed_properties,
                                  GtkAccessibleRelationChange  changed_relations,
                                  GtkAccessibleAttributeSet   *states,
                                  GtkAccessibleAttributeSet   *properties,
                                  GtkAccessibleAttributeSet   *relations)
{
  char *states_str = gtk_accessible_attribute_set_to_string (states);
  char *properties_str = gtk_accessible_attribute_set_to_string (properties);
  char *relations_str = gtk_accessible_attribute_set_to_string (relations);

  GTK_NOTE(A11Y,
    {
      GtkAccessibleRole role = gtk_at_context_get_accessible_role (self);
      GtkAccessible *accessible = gtk_at_context_get_accessible (self);
      GEnumClass *class = g_type_class_ref (GTK_TYPE_ACCESSIBLE_ROLE);
      GEnumValue *value = g_enum_get_value (class, role);
      g_print ("*** Accessible state changed for accessible “%s”, with role “%s” (%d):\n"
           "***     states = %s\n"
           "*** properties = %s\n"
           "***  relations = %s\n",
            G_OBJECT_TYPE_NAME (accessible),
           value->value_nick,
           role,
           states_str,
           properties_str,
           relations_str);
      g_type_class_unref (class);
    });

  g_free (states_str);
  g_free (properties_str);
  g_free (relations_str);
}

static void
gtk_test_at_context_class_init (GtkTestATContextClass *klass)
{
  GtkATContextClass *context_class = GTK_AT_CONTEXT_CLASS (klass);

  context_class->state_change = gtk_test_at_context_state_change;
}

static void
gtk_test_at_context_init (GtkTestATContext *self)
{
}

/*< private >
 * gtk_test_at_context_new:
 * @accessible_role: the #GtkAccessibleRole for the AT context
 * @accessible: the #GtkAccessible instance which owns the AT context
 *
 * Creates a new #GtkTestATContext instance for @accessible, using the
 * given @accessible_role.
 *
 * Returns: (transfer full): the newly created #GtkTestATContext instance
 */
GtkATContext *
gtk_test_at_context_new (GtkAccessibleRole  accessible_role,
                         GtkAccessible     *accessible)
{
  return g_object_new (GTK_TYPE_TEST_AT_CONTEXT,
                       "accessible-role", accessible_role,
                       "accessible", accessible,
                       NULL);
}

gboolean
gtk_test_accessible_has_role (GtkAccessible     *accessible,
                              GtkAccessibleRole  role)
{
  GtkATContext *context;

  g_return_val_if_fail (GTK_IS_ACCESSIBLE (accessible), FALSE);

  context = gtk_accessible_get_at_context (accessible);
  if (context == NULL)
    return FALSE;

  return gtk_at_context_get_accessible_role (context) == role;
}

gboolean
gtk_test_accessible_has_property (GtkAccessible         *accessible,
                                  GtkAccessibleProperty  property)
{
  GtkATContext *context;

  g_return_val_if_fail (GTK_IS_ACCESSIBLE (accessible), FALSE);

  context = gtk_accessible_get_at_context (accessible);
  if (context == NULL)
    return FALSE;

  return gtk_at_context_has_accessible_property (context, property);
}

/**
 * gtk_test_accessible_check_property:
 * @accessible: a #GtkAccessible
 * @property: a #GtkAccessibleProperty
 * @...: the expected value of @property
 *
 * Checks whether the accessible @property of @accessible is set to
 * a specific value.
 *
 * Returns: (transfer full): the value of the accessible property
 */
char *
gtk_test_accessible_check_property (GtkAccessible         *accessible,
                                    GtkAccessibleProperty  property,
                                    ...)
{
  char *res = NULL;
  va_list args;

  va_start (args, property);

  GtkAccessibleValue *check_value =
    gtk_accessible_value_collect_for_property (property, &args);

  va_end (args);

  if (check_value == NULL)
    return g_strdup ("undefined");

  GtkATContext *context = gtk_accessible_get_at_context (accessible);
  GtkAccessibleValue *real_value =
    gtk_at_context_get_accessible_property (context, property);

  if (gtk_accessible_value_equal (check_value, real_value))
    goto out;

  res = gtk_accessible_value_to_string (real_value);

out:
  gtk_accessible_value_unref (check_value);

  return res;
}

gboolean
gtk_test_accessible_has_state (GtkAccessible      *accessible,
                               GtkAccessibleState  state)
{
  GtkATContext *context;

  g_return_val_if_fail (GTK_IS_ACCESSIBLE (accessible), FALSE);

  context = gtk_accessible_get_at_context (accessible);
  if (context == NULL)
    return FALSE;

  return gtk_at_context_has_accessible_state (context, state);
}

/**
 * gtk_test_accessible_check_state:
 * @accessible: a #GtkAccessible
 * @state: a #GtkAccessibleState
 * @...: the expected value of @state
 *
 * Checks whether the accessible @state of @accessible is set to
 * a specific value.
 *
 * Returns: (transfer full): the value of the accessible state
 */
char *
gtk_test_accessible_check_state (GtkAccessible      *accessible,
                                 GtkAccessibleState  state,
                                 ...)
{
  char *res = NULL;
  va_list args;

  va_start (args, state);

  GtkAccessibleValue *check_value =
    gtk_accessible_value_collect_for_state (state, &args);

  va_end (args);

  if (check_value == NULL)
    return g_strdup ("undefined");

  GtkATContext *context = gtk_accessible_get_at_context (accessible);
  GtkAccessibleValue *real_value =
    gtk_at_context_get_accessible_state (context, state);

  if (gtk_accessible_value_equal (check_value, real_value))
    goto out;

  res = gtk_accessible_value_to_string (real_value);

out:
  gtk_accessible_value_unref (check_value);

  return res;
}

gboolean
gtk_test_accessible_has_relation (GtkAccessible         *accessible,
                                  GtkAccessibleRelation  relation)
{
  GtkATContext *context;

  g_return_val_if_fail (GTK_IS_ACCESSIBLE (accessible), FALSE);

  context = gtk_accessible_get_at_context (accessible);
  if (context == NULL)
    return FALSE;

  return gtk_at_context_has_accessible_relation (context, relation);
}

/**
 * gtk_test_accessible_check_relation:
 * @accessible: a #GtkAccessible
 * @relation: a #GtkAccessibleRelation
 * @...: the expected value of @relation
 *
 * Checks whether the accessible @relation of @accessible is set to
 * a specific value.
 *
 * Returns: (transfer full): the value of the accessible relation
 */
char *
gtk_test_accessible_check_relation (GtkAccessible         *accessible,
                                    GtkAccessibleRelation  relation,
                                    ...)
{
  char *res = NULL;
  va_list args;

  va_start (args, relation);

  GtkAccessibleValue *check_value =
    gtk_accessible_value_collect_for_relation (relation, &args);

  va_end (args);

  if (check_value == NULL)
    return g_strdup ("undefined");

  GtkATContext *context = gtk_accessible_get_at_context (accessible);
  GtkAccessibleValue *real_value =
    gtk_at_context_get_accessible_relation (context, relation);

  if (gtk_accessible_value_equal (check_value, real_value))
    goto out;

  res = gtk_accessible_value_to_string (real_value);

out:
  gtk_accessible_value_unref (check_value);

  return res;
}

void
gtk_test_accessible_assertion_message_role (const char        *domain,
                                            const char        *file,
                                            int                line,
                                            const char        *func,
                                            const char        *expr,
                                            GtkAccessible     *accessible,
                                            GtkAccessibleRole  expected_role,
                                            GtkAccessibleRole  actual_role)
{
  char *role_name = g_enum_to_string (GTK_TYPE_ACCESSIBLE_ROLE, actual_role);
  char *s = g_strdup_printf ("assertion failed: (%s): %s.accessible-role = %s (%d)",
                             expr,
                             G_OBJECT_TYPE_NAME (accessible),
                             role_name,
                             actual_role);

  g_assertion_message (domain, file, line, func, s);

  g_free (role_name);
  g_free (s);
}