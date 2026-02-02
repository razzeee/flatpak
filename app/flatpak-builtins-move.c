/* vi:set et sw=2 sts=2 cin cino=t0,f0,(0,{s,>2s,n-s,^-s,e-s:
 * Copyright © 2024 Jules
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
#include "flatpak-cli-transaction.h"
#include "flatpak-quiet-transaction.h"
#include <flatpak-dir-private.h>
#include <flatpak-installation-private.h>
#include "flatpak-error.h"

static char *opt_arch;
static gboolean opt_runtime;
static gboolean opt_app;
static gboolean opt_yes;
static gboolean opt_noninteractive;

static GOptionEntry options[] = {
  { "arch", 0, 0, G_OPTION_ARG_STRING, &opt_arch, N_("Arch to move"), N_("ARCH") },
  { "runtime", 0, 0, G_OPTION_ARG_NONE, &opt_runtime, N_("Look for runtime with the specified name"), NULL },
  { "app", 0, 0, G_OPTION_ARG_NONE, &opt_app, N_("Look for app with the specified name"), NULL },
  { "assumeyes", 'y', 0, G_OPTION_ARG_NONE, &opt_yes, N_("Automatically answer yes for all questions"), NULL },
  { "noninteractive", 0, 0, G_OPTION_ARG_NONE, &opt_noninteractive, N_("Produce minimal output and don't ask questions"), NULL },
  { NULL }
};

typedef struct {
  FlatpakDir *source_dir;
  FlatpakDecomposed *ref;
  char *remote;
} MoveOp;

static void
move_op_free (MoveOp *op)
{
  g_object_unref (op->source_dir);
  flatpak_decomposed_unref (op->ref);
  g_free (op->remote);
  g_free (op);
}

static gboolean
ensure_remote (FlatpakDir *dest_dir,
               FlatpakDir *source_dir,
               const char *remote_name,
               GCancellable *cancellable,
               GError **error)
{
  g_autoptr(GKeyFile) config = NULL;
  g_autoptr(GKeyFile) dest_config = NULL;
  g_autofree char *group = NULL;
  g_autoptr(GError) local_error = NULL;

  if (flatpak_dir_has_remote (dest_dir, remote_name, NULL))
    return TRUE;

  g_info ("Copying remote configuration for %s", remote_name);

  if (!flatpak_dir_ensure_repo (source_dir, cancellable, error))
    return FALSE;
  config = ostree_repo_copy_config (flatpak_dir_get_repo (source_dir));

  group = g_strdup_printf ("remote \"%s\"", remote_name);
  if (!g_key_file_has_group (config, group))
    return flatpak_fail_error (error, FLATPAK_ERROR_REMOTE_NOT_FOUND, _("Remote %s not found in source"), remote_name);

  dest_config = g_key_file_new ();
  /* Copy all keys from source group to dest group */
  g_auto(GStrv) keys = g_key_file_get_keys (config, group, NULL, NULL);
  for (int i = 0; keys && keys[i]; i++)
    {
      g_autofree char *val = g_key_file_get_value (config, group, keys[i], NULL);
      g_key_file_set_value (dest_config, group, keys[i], val);
    }

  if (!flatpak_dir_modify_remote (dest_dir, remote_name, dest_config, NULL, cancellable, error))
    return FALSE;

  if (!flatpak_dir_recreate_repo (dest_dir, cancellable, error))
    return FALSE;

  /* Try to update to get GPG keys etc if they were missing */
  if (!flatpak_dir_update_remote_configuration (dest_dir, remote_name, NULL, NULL, cancellable, &local_error))
    {
       g_info ("Could not update remote %s after copying: %s", remote_name, local_error->message);
       g_clear_error (&local_error);
    }

  return TRUE;
}

gboolean
flatpak_builtin_move (int argc, char **argv, GCancellable *cancellable, GError **error)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GPtrArray) dirs = NULL;
  FlatpakDir *dest_dir;
  g_autoptr(FlatpakInstallation) dest_installation = NULL;
  char **prefs = NULL;
  int i, n_prefs;
  FlatpakKinds kinds;
  g_autoptr(GList) move_ops = NULL;
  GList *l;
  g_autoptr(FlatpakTransaction) transaction = NULL;
  g_autoptr(GHashTable) source_dirs = NULL;
  GHashTableIter hash_iter;
  gpointer key;

  context = g_option_context_new (_("[REF…] - Move applications or runtimes between installations"));
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);

  if (!flatpak_option_context_parse (context, options, &argc, &argv,
                                     FLATPAK_BUILTIN_FLAG_ONE_DIR,
                                     &dirs, cancellable, error))
    return FALSE;

  dest_dir = g_ptr_array_index (dirs, 0);
  dest_installation = flatpak_installation_new_for_dir (dest_dir, cancellable, error);
  if (dest_installation == NULL)
    return FALSE;

  if (argc < 2)
    return usage_error (context, _("At least one REF must be specified"), error);

  if (opt_noninteractive)
    opt_yes = TRUE;

  prefs = &argv[1];
  n_prefs = argc - 1;
  kinds = flatpak_kinds_from_bools (opt_app, opt_runtime);

  source_dirs = g_hash_table_new (g_direct_hash, g_direct_equal);

  for (i = 0; i < n_prefs; i++)
    {
      g_autoptr(FlatpakDecomposed) ref = NULL;
      g_autoptr(FlatpakDir) source_dir = NULL;
      g_autoptr(GError) local_error = NULL;

      source_dir = flatpak_find_installed_pref (prefs[i], kinds, opt_arch, NULL,
                                                TRUE, TRUE, TRUE, NULL,
                                                &ref, cancellable, &local_error);
      if (source_dir == NULL)
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
          return FALSE;
        }

      if (g_file_equal (flatpak_dir_get_path (source_dir), flatpak_dir_get_path (dest_dir)))
        {
          g_printerr (_("Skipping %s: already in destination installation\n"), prefs[i]);
          continue;
        }

      g_autofree char *remote = flatpak_dir_get_origin (source_dir, ref, cancellable, error);
      if (remote == NULL)
        {
          g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
          return FALSE;
        }

      MoveOp *op = g_new0 (MoveOp, 1);
      op->source_dir = g_object_ref (source_dir);
      op->ref = flatpak_decomposed_ref (ref);
      op->remote = g_strdup (remote);
      move_ops = g_list_prepend (move_ops, op);

      g_hash_table_insert (source_dirs, source_dir, source_dir);
    }

  if (move_ops == NULL)
    return TRUE;

  move_ops = g_list_reverse (move_ops);

  /* Start destination transaction */
  if (opt_noninteractive)
    transaction = flatpak_quiet_transaction_new (dest_dir, error);
  else
    transaction = flatpak_cli_transaction_new (dest_dir, opt_yes, TRUE, opt_arch == NULL, error);

  if (transaction == NULL)
    {
      g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
      return FALSE;
    }

  /* Add source repos as sideload repos */
  g_hash_table_iter_init (&hash_iter, source_dirs);
  while (g_hash_table_iter_next (&hash_iter, &key, NULL))
    {
      FlatpakDir *s_dir = key;
      g_autoptr(GFile) repo_path = g_file_get_child (flatpak_dir_get_path (s_dir), "repo");
      g_autofree char *path = g_file_get_path (repo_path);
      flatpak_transaction_add_sideload_repo (transaction, path);
    }

  /* Add installs to transaction */
  for (l = move_ops; l; l = l->next)
    {
      MoveOp *op = l->data;
      if (!ensure_remote (dest_dir, op->source_dir, op->remote, cancellable, error))
        {
          g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
          return FALSE;
        }

      if (!flatpak_transaction_add_install (transaction, op->remote, flatpak_decomposed_get_ref (op->ref), NULL, error))
        {
          g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
          return FALSE;
        }
    }

  if (!flatpak_transaction_run (transaction, cancellable, error))
    {
      g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
      return FALSE;
    }

  /* Now do uninstalls from sources */
  /* We do this one source installation at a time */
  g_hash_table_iter_init (&hash_iter, source_dirs);
  while (g_hash_table_iter_next (&hash_iter, &key, NULL))
    {
      FlatpakDir *s_dir = key;
      g_autoptr(FlatpakTransaction) uninst_transaction = NULL;
      gboolean any_uninst = FALSE;

      if (opt_noninteractive)
        uninst_transaction = flatpak_quiet_transaction_new (s_dir, error);
      else
        uninst_transaction = flatpak_cli_transaction_new (s_dir, opt_yes, TRUE, opt_arch == NULL, error);

      if (uninst_transaction == NULL)
        {
          g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
          return FALSE;
        }

      flatpak_transaction_set_no_pull (uninst_transaction, TRUE);

      for (l = move_ops; l; l = l->next)
        {
          MoveOp *op = l->data;
          if (op->source_dir == s_dir)
            {
              if (!flatpak_transaction_add_uninstall (uninst_transaction, flatpak_decomposed_get_ref (op->ref), error))
                {
                  g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
                  return FALSE;
                }
              any_uninst = TRUE;
            }
        }

      if (any_uninst)
        {
          if (!flatpak_transaction_run (uninst_transaction, cancellable, error))
            {
              g_list_free_full (move_ops, (GDestroyNotify) move_op_free);
              return FALSE;
            }
        }
    }

  /* Migrate overrides */
  for (l = move_ops; l; l = l->next)
    {
      MoveOp *op = l->data;
      if (flatpak_decomposed_is_app (op->ref) &&
          flatpak_dir_is_user (op->source_dir) != flatpak_dir_is_user (dest_dir))
        {
          g_autofree char *id = flatpak_decomposed_dup_id (op->ref);
          gsize metadata_size;
          g_autoptr(GError) local_error = NULL;
          g_autofree char *metadata_contents = flatpak_dir_load_override (op->source_dir, id, &metadata_size, NULL, &local_error);
          if (metadata_contents)
            {
              g_autoptr(GKeyFile) metakey = g_key_file_new ();
              if (g_key_file_load_from_data (metakey, metadata_contents, metadata_size, 0, NULL))
                {
                   if (!flatpak_save_override_keyfile (metakey, id, flatpak_dir_is_user (dest_dir), &local_error))
                     g_warning ("Failed to migrate overrides for %s: %s", id, local_error->message);
                   else
                     {
                       /* Remove from source if successfully migrated */
                       if (!flatpak_remove_override_keyfile (id, flatpak_dir_is_user (op->source_dir), &local_error))
                         g_warning ("Failed to remove source overrides for %s: %s", id, local_error->message);
                     }
                }
            }
        }
    }

  g_list_free_full (move_ops, (GDestroyNotify) move_op_free);

  return TRUE;
}

gboolean
flatpak_complete_move (FlatpakCompletion *completion)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GPtrArray) dirs = NULL;
  int i;
  FlatpakKinds kinds;

  context = g_option_context_new ("");
  if (!flatpak_option_context_parse (context, options, &completion->argc, &completion->argv,
                                     FLATPAK_BUILTIN_FLAG_ALL_DIRS | FLATPAK_BUILTIN_FLAG_OPTIONAL_REPO,
                                     &dirs, NULL, NULL))
    return FALSE;

  kinds = flatpak_kinds_from_bools (opt_app, opt_runtime);

  switch (completion->argc)
    {
    case 0:
    default: /* REF */
      flatpak_complete_options (completion, global_entries);
      flatpak_complete_options (completion, options);
      flatpak_complete_options (completion, user_entries);

      for (i = 0; i < dirs->len; i++)
        {
          FlatpakDir *dir = g_ptr_array_index (dirs, i);
          flatpak_complete_partial_ref (completion, kinds, opt_arch, dir, NULL);
        }

      break;
    }

  return TRUE;
}
