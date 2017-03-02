#include "fs.h"

#include <program.h>
#include <stdio.h>

#include <argdata.hpp>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <boost/test/unit_test.hpp>

boost::unit_test::test_suite *init_unit_test_suite(int argc, char *argv[]);

void SetTempPath(const fs::path &p);

void program_main(const argdata_t *ad) {
    std::vector<char *> arguments{const_cast<char *>("argv0")};
    for (auto[key, value] : ad->as_map()) {
        std::string_view keystr = key->as_str();
        if (keystr == "terminal") {
            // boost::test prints output to stderr. Let stderr use to the file
            // descriptor provided.
            FILE *fp = fdopen(value->as_fd(), "w");
            if (fp != nullptr) {
                fswap(fp, stderr);
                fclose(fp);
                setvbuf(stderr, NULL, _IONBF, 0);
            }
        } else if (keystr == "arguments") {
            // Command line arguments that need to get forwarded to the unit
            // testing framework.
            for (auto arg : value->as_seq()) {
                const char *argstr;
                if (argdata_get_str_c(arg, &argstr) == 0) {
                    arguments.push_back(const_cast<char *>(argstr));
                }
            }
        } else if (keystr == "tempdir") {
            SetTempPath(fs::path(value->as_fd(), "."));
        }
    }
    arguments.push_back(nullptr);

    // Invoke Boost's unit testing entry point.
    std::exit(boost::unit_test::unit_test_main(
        &init_unit_test_suite, arguments.size() - 1, arguments.data()));
}
