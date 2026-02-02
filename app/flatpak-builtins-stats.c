/* vi:set et sw=2 sts=2 cin cino=t0,f0,(0,{s,>2s,n-s,^-s,e-s:
 * Copyright © 2021 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <glib/gi18n.h>

#include "libglnx.h"

#include "flatpak-builtins.h"
#include "flatpak-builtins-utils.h"
#include "flatpak-utils-private.h"

static gboolean opt_user;
static gboolean opt_system;
static char **opt_installations;

static GOptionEntry options[] = {
  { "user", 0, 0, G_OPTION_ARG_NONE, &opt_user, N_("Show user installations"), NULL },
  { "system", 0, 0, G_OPTION_ARG_NONE, &opt_system, N_("Show system-wide installations"), NULL },
  { "installation", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_installations, N_("Show specific system-wide installations"), N_("NAME") },
  { NULL }
};

gboolean
flatpak_builtin_stats (int argc, char **argv, GCancellable *cancellable, GError **error)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GPtrArray) dirs = NULL;
  int i;

  context = g_option_context_new (_(" - Show statistics about installations"));
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);

  if (!flatpak_option_context_parse (context, options, &argc, &argv,
                                     FLATPAK_BUILTIN_FLAG_ALL_DIRS,
                                     &dirs, cancellable, error))
    return FALSE;

  for (i = 0; i < dirs->len; i++)
    {
      FlatpakDir *dir = g_ptr_array_index (dirs, i);
      g_autofree char *name = flatpak_dir_get_name (dir);
      g_autofree char *path = g_file_get_path (flatpak_dir_get_path (dir));
      FlatpakDirStats stats;

      if (!flatpak_dir_get_stats (dir, &stats, cancellable, error))
        return FALSE;

      g_print (_("Installation: %s (%s)\n"), name, path);

      g_autofree char *total_size_s = g_format_size (stats.total_size);
      g_autofree char *exclusive_size_s = g_format_size (stats.exclusive_size);
      g_autofree char *shared_size_s = g_format_size (stats.shared_size);

      g_print (_("  Total size on disk: %s\n"), total_size_s);
      g_print (_("  Apps: %d\n"), stats.n_apps);
      g_print (_("  Runtimes: %d\n"), stats.n_runtimes);
      g_print (_("  Exclusive size: %s\n"), exclusive_size_s);
      g_print (_("  Shared size: %s\n"), shared_size_s);
      g_print ("\n");
    }

  return TRUE;
}

gboolean
flatpak_complete_stats (FlatpakCompletion *completion)
{
  flatpak_complete_options (completion, global_entries);
  flatpak_complete_options (completion, options);
  flatpak_complete_options (completion, user_entries);
  return TRUE;
}
