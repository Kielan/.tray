#include "testing/testing.h"

#include "mem_guardedalloc.h"

DEFINE_string(test_assets_dir, "", "lib/tests directory from SVN containing the test assets.");
DEFINE_string(test_release_dir, "", "bin/{tray version} directory of the current build.");

namespace tray::tests {

const std::string &flags_test_asset_dir()
{
  if (FLAGS_test_assets_dir.empty()) {
    ADD_FAILURE()
        << "Pass the flag --test-assets-dir and point to the lib/tests directory from SVN.";
  }
  return FLAGS_test_assets_dir;
}

const std::string &flags_test_release_dir()
{
  if (FLAGS_test_release_dir.empty()) {
    ADD_FAILURE()
        << "Pass the flag --test-release-dir and point to the bin/{tray version} directory.";
  }
  return FLAGS_test_release_dir;
}

}  // namespace tray::tests

int main(int argc, char **argv)
{
  mem_use_guarded_allocator();
  mem_init_memleak_detection();
  mem_enable_fail_on_memleak();
  testing::InitGoogleTest(&argc, argv);
  TRAY_GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  return RUN_ALL_TESTS();
}
