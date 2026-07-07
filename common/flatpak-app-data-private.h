/* vi:set et sw=2 sts=2 cin cino=t0,f0,(0,{s,>2s,n-s,^-s,e-s:
 * Copyright © 2026 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#pragma once

#include <gio/gio.h>

gboolean flatpak_delete_app_data           (const char  *app_id,
                                            GError     **error);
gboolean flatpak_reset_permissions_for_app (const char  *app_id,
                                            GError     **error);
