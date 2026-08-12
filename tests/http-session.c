#include "libglnx.h"
#include "common/flatpak-utils-http-private.h"

int
main (int argc, char *argv[])
{
  g_autoptr(FlatpakHttpSession) session = NULL;
  g_autoptr(FlatpakCertificates) first_certificates = NULL;
  g_autoptr(FlatpakCertificates) second_certificates = NULL;
  g_autoptr(GBytes) first = NULL;
  g_autoptr(GBytes) second = NULL;
  g_autoptr(GError) error = NULL;

  if (argc != 3)
    {
      g_printerr ("Usage: http-session FIRST_URL SECOND_URL\n");
      return 1;
    }

  session = flatpak_create_http_session (PACKAGE_STRING);
  first_certificates = flatpak_get_certificates_for_uri (argv[1], &error);
  if (first_certificates == NULL)
    {
      g_printerr ("Failed to load certificates: %s\n", error->message);
      return 1;
    }

  first = flatpak_load_uri_full (session, argv[1], first_certificates, 0,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, &error);
  if (first == NULL)
    {
      g_printerr ("First request failed: %s\n", error->message);
      return 1;
    }

  second_certificates = flatpak_get_certificates_for_uri (argv[2], &error);
  if (second_certificates == NULL)
    {
      g_printerr ("Failed to load certificates: %s\n", error->message);
      return 1;
    }

  second = flatpak_load_uri_full (session, argv[2], second_certificates, 0,
                                  NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                  NULL, &error);
  if (second == NULL)
    {
      g_printerr ("Second request failed: %s\n", error->message);
      return 1;
    }

  return 0;
}
