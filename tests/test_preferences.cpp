// SPDX-License-Identifier: GPL-3.0-or-later
#include "preferences.hpp"
#include "sponsor_notice.hpp"
#include <cassert>
#include <cstdlib>
#include <limits>
#include <string>
int main()
{
    char pattern[] = "/tmp/tube5-preferences-XXXXXX";
    const char *root = ::mkdtemp(pattern);
    assert(root);
    const std::string directory = std::string(root) + "/settings";
    const std::string path = directory + "/settings.conf";
    tube5::Preferences store(directory.c_str());
    bool enabled = false;
    assert(store.load(enabled) == tube5::Preferences::Load::missing && enabled);
    assert(store.save(false));
    tube5::Preferences reopened(directory.c_str());
    assert(reopened.load(enabled) == tube5::Preferences::Load::loaded && !enabled);
    assert(store.save(true));
    assert(reopened.load(enabled) == tube5::Preferences::Load::loaded && enabled);
    struct stat info{};
    assert(::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    FILE *file = std::fopen(path.c_str(), "wb");
    assert(file && std::fputs("TUBE5_SETTINGS_V2\nSPONSORBLOCK=0\n", file) >= 0);
    assert(std::fclose(file) == 0);
    assert(store.load(enabled) == tube5::Preferences::Load::invalid && enabled);
    assert(store.save(false));
    assert(store.load(enabled) == tube5::Preferences::Load::loaded && !enabled);
    const std::string temporary = directory + "/settings.tmp";
    assert(::symlink(path.c_str(), temporary.c_str()) == 0);
    assert(!store.save(true));
    assert(store.load(enabled) == tube5::Preferences::Load::loaded && !enabled);
    assert(::unlink(temporary.c_str()) == 0);
    assert(::unlink(path.c_str()) == 0);
    assert(::mkfifo(path.c_str(), 0600) == 0);
    assert(store.load(enabled) == tube5::Preferences::Load::invalid);
    assert(::unlink(path.c_str()) == 0);
    assert(::rmdir(directory.c_str()) == 0 && ::rmdir(root) == 0);
    tube5::Preferences bad(nullptr);
    assert(!bad.save(false));
    assert(bad.load(enabled) == tube5::Preferences::Load::unavailable && enabled);
    tube5::SponsorNotice notice;
    assert(!notice.visible(0));
    notice.begin(0, 4);
    assert(!notice.visible(100));
    notice.complete(100);
    assert(notice.seconds() == 4 && notice.visible(2599) && !notice.visible(2600));
    notice.complete(2000);
    assert(!notice.visible(2600));
    notice.begin(809.3, 887);
    notice.complete(0xfffffff0u);
    assert(notice.seconds() == 78 && notice.visible(100));
    notice.cancel();
    assert(!notice.visible(100));
    notice.begin(0, std::numeric_limits<double>::quiet_NaN());
    notice.complete(0);
    assert(!notice.visible(0));
}
