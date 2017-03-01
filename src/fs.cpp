#include "fs.h"

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>

static const char PATHSEP = '/';
static const int FD_NONE = -1;

namespace fs {

/** path implementation */

path::path():
    f(FD_NONE)
{
}

path::path(int fd, const std::string &p_in):
    f(fd), p(p_in)
{
    normalize();
}

path::path(const std::string &p_in):
    f(FD_NONE), p(p_in)
{
    normalize();
}

path::path(const char *p_in):
    f(FD_NONE), p(p_in)
{
    normalize();
}

path path::parent_path() const
{
    // Remove last path element
    size_t n = p.rfind(PATHSEP);
    if (n == std::string::npos || n == 0) {
        // No path separator at all, or found a path separator at first position
        // which means we're already at the root. According to the filesystem doc
        // this should return empty path.
        return path(f, "");
    } else {
        // Return path up to last path separator
        return path(f, p.substr(0, n));
    }
}

bool path::is_complete() const
{
    return !p.empty() && p[0] == PATHSEP;
}

bool path::empty() const
{
    return p.empty();
}

path& path::operator/=(const path& p_in)
{
    // Assert that concatenated path does not have an associated fd
    assert(p_in.f == FD_NONE);
    // Need to add a path separator?
    if (p.size() > 0 && p[p.size()-1] != PATHSEP) {
        p += PATHSEP;
    }
    p += p_in.p;
    normalize();
    return *this;
}

void path::normalize()
{
    // Remove trailing path separator, if this path is not the root
    if (p.empty() || (p.size() == 1 && p[0] == PATHSEP))
        return;
    if (p[p.size()-1] == PATHSEP) {
        p.resize(p.size() - 1);
    }
}

static std::string debug_str(const path &p)
{
    std::stringstream rv;
    rv << "[" << p.fd() << "]" << p.string();
    return rv.str();
}

/** operations */

enum class status {
    file_not_found,
    regular_file,
    directory_file,
    unknown_file

};
/* helper */
static enum status path_status(const path& p)
{
    struct stat buf;
    if (fstatat(p.fd(), p.string().c_str(), &buf, 0) < 0) {
        if (errno == ENOENT) {
            return status::file_not_found;
        }
        throw filesystem_error("cannot stat " + debug_str(p) + ": " + strerror(errno));
    }
    if (S_ISREG(buf.st_mode)) {
        return status::regular_file;
    }
    if (S_ISDIR(buf.st_mode)) {
        return status::directory_file;
    }
    return status::unknown_file;
}

bool create_directories(const path& p)
{
    // Shortcut: already exists?
    if (is_directory(p)) {
        return false;
    }
    std::string s = p.string();
    size_t ptr = 0;
    while (ptr < s.size()) {
        size_t next = s.find(PATHSEP, ptr);
        if (next == std::string::npos) {
            next = s.size();
        }
        path subpath = path(p.fd(), s.substr(0, next));
        status subpath_status = path_status(subpath);
        if (subpath_status == status::file_not_found) {
            // Not existing, try to create
            create_directory(subpath);
        } else if (subpath_status != status::directory_file) {
            // Exists but is not a directory
            throw filesystem_error("directory component " + debug_str(subpath) + " already exists and is not a directory");
        }
        if (next == std::string::npos) {
            break;
        } else {
            ptr = next + 1;
        }
    }
    return false;
}

bool create_directory(const path& p)
{
    if (mkdirat(p.fd(), p.string().c_str(), 0777) < 0) {
        throw filesystem_error("cannot make directory " + debug_str(p));
    }
    return true;
}

uint64_t remove_all(const path& p)
{
    /* TODO recursive delete */
    return 0;
}

uint64_t file_size(const path& p)
{
    struct stat buf;
    if (fstatat(p.fd(), p.string().c_str(), &buf, 0) < 0) {
        throw filesystem_error("cannot stat " + debug_str(p) + ": " + strerror(errno));
    }
    return buf.st_size;
}

bool remove(const path& p)
{
    if (unlinkat(p.fd(), p.string().c_str(), 0) < 0) {
        throw filesystem_error("cannot unlink " + debug_str(p));
    }
    return true;
}

bool exists(const path& p)
{
    return path_status(p) != status::file_not_found;
}

bool is_directory(const path& p)
{
    return path_status(p) == status::directory_file;
}

void rename(const path& old_p, const path& new_p)
{
    if (renameat(old_p.fd(), old_p.string().c_str(),
                 new_p.fd(), new_p.string().c_str()) < 0) {
        throw filesystem_error("cannot rename " + debug_str(old_p) + " to " + debug_str(new_p));
    }
}

space_info space(const path& p) {
    space_info rv;
    /* TODO */
    rv.available = 1000ULL*1000ULL*1000ULL*1000ULL;
    return rv;
}

path system_complete(const path& p)
{
    return p;
}

} // namespace fs

namespace fsbridge {

FILE *fopen(const fs::path& p, const char *mode)
{
    return ::fopenat(p.fd(), p.string().c_str(), mode);
}

FILE *freopen(const fs::path& p, const char *mode, FILE *stream)
{
    FILE *f = ::fopenat(p.fd(), p.string().c_str(), mode);
    if (f == NULL) {
        return NULL;
    }
    ::fswap(stream, f);
    ::fclose(f);
    return stream;
}

} // fsbridge
