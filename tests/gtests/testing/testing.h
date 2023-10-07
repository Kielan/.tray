#ifndef __TRAY_TESTING_H__
#define __TRAY_TESTING_H__

#include <vector>

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "gtest/gtest.h"

namespace tray::tests {

/* These strings are passed on the CLI with the --test-asset-dir and --test-release-dir arguments.
 * The arguments are added automatically when invoking tests via `ctest`. */
const std::string &flags_test_asset_dir();   /* ../lib/tests in the SVN directory. */
const std::string &flags_test_release_dir(); /* bin/{tray version} in the build dir. */

}  // namespace tray::tests
