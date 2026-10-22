## Compiling Flatpak

If you need to build Flatpak from source, you can do so with Meson or
with GNU Autotools. The recommended build system for this version of
Flatpak is Meson, and the Autotools build system is likely to be removed
from a future version of Flatpak.

The exact steps required depend on your distribution. Below are some
steps that should work on Debian and Fedora, based on the configure
options used to build those distributions' packages, These options will
install into `/usr`, which will overwrite your distribution-provided
system copy of Flatpak.
**You should only do this if you understand the risks of it to the
stability of your system, and you probably want to do it in a VM or on
a development machine that's expected to break sometimes!**

### On Debian

Make sure that `deb-src` apt sources are enabled in `/etc/apt/sources.list.d/*.sources` or `/etc/apt/sources.list`. 
See distribution documentation for details.

Then use commands similar to these:

```
git clone https://github.com/flatpak/flatpak
cd flatpak
sudo apt build-dep flatpak
git submodule update --init
meson setup --prefix=/usr --sysconfdir=/etc --localstatedir=/var -Dselinux_module=disabled -Dinstalled_tests=true -Ddbus_config_dir=/usr/share/dbus-1/system.d -Dprivileged_group=sudo -Drun_media_dir=/media -Dsystem_bubblewrap=bwrap -Dsystem_dbus_proxy=xdg-dbus-proxy -Dsystemdsystemunitdir=/lib/systemd/system -Dsystemdsystemenvgendir=/lib/systemd/system-environment-generators -Dsystem_helper_user=_flatpak -Dgtkdoc=disabled _build
meson compile -C _build
meson test -C _build
sudo meson install -C _build
```

Note: Older versions of Ubuntu/Debian, such as `Ubuntu 24.04`, may require also installing `meson` and cannot use the system `bwrap`.

### On Fedora

```
git clone https://github.com/flatpak/flatpak
cd flatpak
sudo dnf builddep flatpak
sudo dnf install gettext-devel socat ostree
git submodule update --init
meson setup --prefix=/usr --sysconfdir=/etc --localstatedir=/var -Dinstalled_tests=true -Dselinux_module=enabled -Dsystem_bubblewrap=bwrap -Dsystem_dbus_proxy=xdg-dbus-proxy _build
meson compile -C _build
meson test -C _build
sudo meson install -C _build
```

## How to run a specified set of tests

Sometimes you don't want to run the whole test suite but just one you're
working on. This can be accomplished with a command like:

```
meson test -C _build test-info@user.wrap test-info@system.wrap
```

## Measuring test coverage

The GCC coverage job in `.github/workflows/check.yml` runs the Meson C and
shell integration tests with `-Db_coverage=true`. It runs separately from the
sanitizer job and reports line and branch coverage without a minimum threshold.
Download the `coverage` artifact and open `coverage/index.html` to see coverage
for each source file. The artifact also contains Cobertura XML, a text report,
a summary, and Meson test logs. Failed test runs produce partial reports.

To run this locally, install the build and test dependencies, GCC, and a
matching version of gcov. Use a gcovr version that supports your GCC version.
Ubuntu 24.04 CI uses its packaged gcovr 7.0. Local verification with GCC 16
required gcovr 8.7.

```sh
CC=gcc CFLAGS=-fprofile-update=atomic meson setup --buildtype=debug \
  -Db_coverage=true -Dgir=disabled -Dapi_docs=disabled \
  -Dinternal_checks=true -Dinternal_tests=true \
  -Dsystem_dbus_proxy=xdg-dbus-proxy _build-coverage
meson compile -C _build-coverage
meson compile -C _build-coverage runtime-repo
find _build-coverage -name '*.gcda' -delete
LC_ALL=en_US.UTF-8 meson test -C _build-coverage
mkdir -p _build-coverage/coverage
gcovr --root . --exclude 'tests/' --exclude 'subprojects/' \
  --exclude '_build-coverage/' \
  --html-details _build-coverage/coverage/index.html \
  --xml-pretty --xml _build-coverage/coverage/coverage.xml \
  --txt _build-coverage/coverage/coverage.txt \
  --print-summary _build-coverage
```

Use a fresh build directory. Fixture generation runs Flatpak, so reset counters
after building `runtime-repo`. gcovr scans the build tree for counters from the
CLI, libraries, and helpers that integration tests run. It uses `.gcno` files
to include compiled code that no test executed. The report excludes test
sources, vendored subprojects, and generated sources in the build directory.
Atomic updates prevent threads from losing counter increments. GCC uses file
locking to merge counters from processes that exit normally.

The results cover only this build configuration and the tests that ran.
Processes may not save counters if a test kills them or if their sandbox or
user permissions prevent writes to the build tree. This can leave gaps in
service and helper coverage.

Integration tests need working user namespaces, D-Bus, and the runtime
dependencies listed in CI. See the workflow for dependency and AppArmor setup.
If local restrictions prevent tests from running, list the skipped or failed
tests with your coverage results. A unit-only run does not measure the same
code paths as CI.

## Commit messages

Start the subject with the affected component when practical, for example
`run:`, `tests:`, or `dir:`, followed by a capitalized, imperative summary.
Keep subjects and body text to 72 characters per line where possible so they
remain readable in an 80-column terminal.

Use the body to explain why the change is needed and any behavior that is not
obvious from the diff.

## More info
Dependencies you will need include: meson, bison,
gettext, gtk-doc, gobject-introspection, libcap, libarchive, libxml2, libcurl,
gpgme, polkit, libXau, ostree, json-glib, appstream, libseccomp (or their devel
packages).

Most configure arguments are documented in `meson_options.txt`. However,
there are some options that are a bit more complicated.

Flatpak relies on a project called
[Bubblewrap](https://github.com/containers/bubblewrap) for the low-level
sandboxing. By default, an in-tree copy of this is built (distributed in the
tarball or using git submodules in the git tree). This will build a helper
called flatpak-bwrap. If your system has a recent enough version of Bubblewrap
already, you can use `-Dsystem_bubblewrap=bwrap` to use that instead.

Bubblewrap can run in two modes, either using unprivileged user
namespaces or setuid mode. This requires that the kernel supports this,
which some distributions disable. For instance, Debian and Arch
([linux](https://www.archlinux.org/packages/?name=linux) kernel v4.14.5
or later), support user namespaces with the `kernel.unprivileged_userns_clone`
sysctl enabled.

If unprivileged user namespaces are not available, then Bubblewrap must
be built as setuid root. This is believed to be safe, as it is
designed to do this. Any build of Bubblewrap supports both
unprivileged and setuid mode, you just need to set the setuid bit for
it to change mode. The Meson build does not do this automatically.

There are some complications when building Flatpak to a different
prefix than the system-installed version. First of all, the newly
built Flatpak will look for system-installed flatpaks in
`$PREFIX/var/lib/flatpak`, which will not match existing installations.
You can use `-Dsystem_install_dir=/var/lib/flatpak` to make both
installations use the same location.

Secondly, Flatpak ships with a root-privileged PolicyKit helper for
system-wide installation, called `flatpak-system-helper`. It is D-Bus
activated (on the system bus) and if you install in a non-standard
location it is likely that D-Bus will not find it and PolicyKit
integration will not work. However, if the system installation is
synchronized, you can often use the system installed helper instead—
at least if the two versions are close enough.

## This repository

The Flatpak project consists of multiple pieces, and it can be
a bit challenging to find your way around at first. Here is a
quick intro to each of the important subdirectories:
* `app`: the commandline client. Each command has a `flatpak-builtins-` source file
* `common`: contains the library, libflatpak. It also contains various pieces
  of code that are shared between the library, the client and the services.
  Non-public code can be recognized by having a `-private.h` header file.
* `completion`: commandline auto completion support
* `data`: D-Bus interface definition files and GVariant schemas
* `doc`: The sources for the documentation, both man pages and library documentation
* `icon-validator`: A small utility that is used to validate icons
* `oci-authenticator`: service used for authenticating the user for installing
  from oci remotes (e.g. for paid apps)
* `po`: translations
* `portal`: The Flatpak portal service, which lets sandboxed apps request the
  creation of new sandboxes
* `revokefs`: A FUSE filesystem that is used to transfer files downloaded by
  the user to the system-helper without copying
* `session-helper`: The flatpak-session-helper service, which provides various
  helpers for the sandbox setup at runtime
* `tests`: The testsuite
* `subprojects/bubblewrap`: Flatpak's unprivileged sandboxing tool which is
  developed separately and exists here as a submodule
* `subprojects/libglnx`: a small utility library for projects that use GLib on
  Linux, as a submodule
* `subprojects/dbus-proxy`: a filtering proxy for D-Bus connections, as a submodule
* `subprojects/variant-schema-compiler`: a tool for generating code to
  efficiently access data encoded using GVariant, as a submodule
* `system-helper`: The flatpak-system-helper service, which runs as root on the
  system bus and allows non-root users to modify system installations
