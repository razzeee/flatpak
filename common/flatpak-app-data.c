#include "config.h"

#include "flatpak-app-data-private.h"
#include "flatpak-utils-private.h"

#define PERMISSION_STORE_BUS_NAME "org.freedesktop.impl.portal.PermissionStore"
#define PERMISSION_STORE_OBJECT_PATH "/org/freedesktop/impl/portal/PermissionStore"
#define PERMISSION_STORE_INTERFACE "org.freedesktop.impl.portal.PermissionStore"

static char **
get_permission_tables(void)
{
  g_autofree char *path = NULL;
  g_autoptr(GPtrArray) tables = NULL;
  GDir *dir;
  const char *name;

  tables = g_ptr_array_new_with_free_func(g_free);

  path = g_build_filename(g_get_user_data_dir(), "flatpak/db", NULL);
  dir = g_dir_open(path, 0, NULL);
  if (dir != NULL)
  {
    while ((name = g_dir_read_name(dir)) != NULL)
      g_ptr_array_add(tables, g_strdup(name));

    g_dir_close(dir);
  }

  g_ptr_array_add(tables, NULL);

  return (char **)g_ptr_array_free(g_steal_pointer(&tables), FALSE);
}

static gboolean
remove_permissions_for_app(GDBusProxy *store,
                           const char *table,
                           const char *app_id,
                           GError **error)
{
  g_autoptr(GVariant) list_reply = NULL;
  g_auto(GStrv) ids = NULL;

  /* FIXME some portals cache their permission tables and assume that they're
   * the only writers, so they may miss these changes.
   * See https://github.com/flatpak/xdg-desktop-portal/issues/197
   */
  list_reply = g_dbus_proxy_call_sync(store, "List",
                                      g_variant_new("(s)", table),
                                      G_DBUS_CALL_FLAGS_NONE, -1,
                                      NULL, error);
  if (list_reply == NULL)
    return FALSE;

  g_variant_get(list_reply, "(^as)", &ids);

  for (int i = 0; ids[i]; i++)
  {
    g_autoptr(GVariant) lookup_reply = NULL;
    g_autoptr(GVariant) permissions = NULL;
    g_autoptr(GVariant) data = NULL;
    GVariantIter iter;
    char *key;
    GVariant *value;
    g_auto(GVariantBuilder) builder = FLATPAK_VARIANT_BUILDER_INITIALIZER;
    gboolean need_to_set = FALSE;

    lookup_reply = g_dbus_proxy_call_sync(store, "Lookup",
                                          g_variant_new("(ss)", table, ids[i]),
                                          G_DBUS_CALL_FLAGS_NONE, -1,
                                          NULL, error);
    if (lookup_reply == NULL)
      return FALSE;

    g_variant_get(lookup_reply, "(@a{sas}@v)", &permissions, &data);

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sas}"));
    g_variant_iter_init(&iter, permissions);
    while (g_variant_iter_loop(&iter, "{s@as}", &key, &value))
    {
      if (app_id == NULL || strcmp(key, app_id) == 0)
      {
        need_to_set = TRUE;
        continue;
      }

      g_variant_builder_add(&builder, "{s@as}", key, value);
    }

    if (need_to_set)
    {
      g_autoptr(GVariant) set_reply = NULL;

      set_reply = g_dbus_proxy_call_sync(store, "Set",
                                         g_variant_new("(sbs@a{sas}@v)",
                                                       table, TRUE, ids[i],
                                                       g_variant_builder_end(&builder),
                                                       g_steal_pointer(&data)),
                                         G_DBUS_CALL_FLAGS_NONE, -1,
                                         NULL, error);
      if (set_reply == NULL)
        return FALSE;
    }
  }

  return TRUE;
}

gboolean
flatpak_reset_permissions_for_app(const char *app_id,
                                  GError **error)
{
  g_autoptr(GDBusConnection) session_bus = NULL;
  g_autoptr(GDBusProxy) store = NULL;
  g_auto(GStrv) tables = NULL;

  session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, error);
  if (session_bus == NULL)
    return FALSE;

  store = g_dbus_proxy_new_sync(session_bus, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                PERMISSION_STORE_BUS_NAME,
                                PERMISSION_STORE_OBJECT_PATH,
                                PERMISSION_STORE_INTERFACE,
                                NULL, error);
  if (store == NULL)
    return FALSE;

  tables = get_permission_tables();

  for (int i = 0; tables[i]; i++)
  {
    if (!remove_permissions_for_app(store, tables[i], app_id, error))
      return FALSE;
  }

  return TRUE;
}

gboolean
flatpak_delete_app_data(const char *app_id,
                        GError **error)
{
  g_autofree char *path = g_build_filename(g_get_home_dir(), ".var", "app", app_id, NULL);
  g_autoptr(GFile) file = g_file_new_for_path(path);

  if (g_file_query_exists(file, NULL))
  {
    if (!flatpak_rm_rf(file, NULL, error))
      return FALSE;
  }

  return flatpak_reset_permissions_for_app(app_id, error);
}
