// Copyright 2019 Pokitec
// All rights reserved.

#include "TTauri/Foundation/URL.hpp"
#include "TTauri/Foundation/globals.hpp"
#include "TTauri/Foundation/strings.hpp"
#include "TTauri/Foundation/required.hpp"
#include "TTauri/Foundation/url_parser.hpp"
#include <regex>

namespace TTauri {

URL::URL(std::string_view url) :
    value(normalize_url(url))
{
}

URL::URL(char const *url) :
    value(normalize_url(url))
{
}

URL::URL(std::string const &url) :
    value(normalize_url(url))
{
}

URL::URL(url_parts const &parts) :
    value(generate_url(parts))
{
}

size_t URL::hash() const noexcept
{
    return std::hash<std::string>{}(value);
}

std::string URL::string() const noexcept
{
    return value;
}

std::string_view URL::scheme() const noexcept
{
    return parse_url(value).scheme;
}

std::string URL::query() const noexcept
{
    return url_decode(parse_url(value).query, true);
}

std::string URL::fragment() const noexcept
{
    return url_decode(parse_url(value).fragment);
}

std::string URL::filename() const noexcept
{
    let parts = parse_url(value);
    if (parts.segments.size() > 0) {
        return url_decode(parts.segments.back());
    } else {
        return {};
    }
}

std::string URL::directory() const noexcept
{
    auto parts = parse_url(value);
    if (parts.segments.size() > 0) {
        parts.segments.pop_back();
    }
    return generate_path(parts);
}

std::string URL::nativeDirectory() const noexcept
{
    auto parts = parse_url(value);
    if (parts.segments.size() > 0) {
        parts.segments.pop_back();
    }
    return generate_native_path(parts);
}

std::string URL::extension() const noexcept
{
    let fn = filename();
    let i = fn.rfind('.');
    return fn.substr((i != fn.npos) ? (i + 1) : fn.size());
}

std::vector<std::string> URL::pathSegments() const noexcept
{
    let parts = parse_url(value);
    return transform<std::vector<std::string>>(parts.segments, [](auto x) {
        return url_decode(x);
        });
}

std::string URL::path() const noexcept
{
    return generate_path(parse_url(value));
}

std::string URL::nativePath() const noexcept
{
    return generate_native_path(parse_url(value));
}

std::wstring URL::nativeWPath() const noexcept
{
    return translateString<std::wstring>(nativePath());
}

bool URL::isAbsolute() const noexcept
{
    return parse_url(value).absolute;
}

bool URL::isRelative() const noexcept
{
    return !isAbsolute();
}

bool URL::containsWildCard() const noexcept
{
    for (let &segment: pathSegments()) {
        for (let c: segment) {
            if (c == '*' || c == '?') {
                return true;
            }
        }
    }
    return false;
}

URL URL::urlByAppendingPath(URL const &other) const noexcept
{
    let this_parts = parse_url(value);
    let other_parts = parse_url(other.value);
    let new_parts = concatenate_url_parts(this_parts, other_parts);
    return URL(new_parts);
}

URL URL::urlByAppendingPath(std::string_view const other) const noexcept
{
    return urlByAppendingPath(URL::urlFromPath(other));
}

URL URL::urlByAppendingPath(std::string const &other) const noexcept
{
    return urlByAppendingPath(URL::urlFromPath(other));
}

URL URL::urlByAppendingPath(char const *other) const noexcept
{
    return urlByAppendingPath(URL::urlFromPath(other));
}

URL URL::urlByAppendingPath(std::wstring_view const other) const noexcept
{
    return urlByAppendingPath(URL::urlFromWPath(other));
}

URL URL::urlByAppendingPath(std::wstring const &other) const noexcept
{
    return urlByAppendingPath(URL::urlFromWPath(other));
}

URL URL::urlByAppendingPath(wchar_t const *other) const noexcept
{
    return urlByAppendingPath(URL::urlFromWPath(other));
}

URL URL::urlByRemovingFilename() const noexcept
{
    auto parts = parse_url(value);
    if (parts.segments.size() > 0) {
        parts.segments.pop_back();
    }
    return URL(parts);
}

static bool containsWildcards(std::string const &s)
{
    for (let c: s) {
        if (c == '*' || c == '?') {
            return true;
        }
    }
    return false;
}


static bool urlMatchGlob(URL url, URL glob, bool exactMatch) noexcept
{
    let urlSegments = url.pathSegments();
    let globSegments = glob.pathSegments();

    size_t globIndex = 0;
    for (; globIndex < globSegments.size(); globIndex++) { 

    }

    return !exactMatch || globIndex == globSegments.size();
}

static void urlsByRecursiveScanning(URL base, URL glob, std::vector<URL> &result) noexcept
{
    for (let &filename: base.filenamesByScanningDirectory()) {
        if (filename.back() == '/') {
            let directory = std::string_view(filename.data(), filename.size() - 1);
            let recurseURL = base.urlByAppendingPath(directory);
            if (urlMatchGlob(recurseURL, glob, false)) {
                urlsByRecursiveScanning(recurseURL, glob, result);
            }

        } else {
            let finalURL = base.urlByAppendingPath(filename);
            if (urlMatchGlob(finalURL, glob, true)) {
                result.push_back(finalURL);
            }
        }
    }
}

static URL urlBaseFromGlob(URL glob) noexcept
{
    auto base = glob;
    while (base.containsWildCard()) {
        base = base.urlByRemovingFilename();
    }
    return base;
}

std::vector<URL> URL::urlsByScanningWithGlobPattern() const noexcept
{
    std::vector<URL> urls;
    urlsByRecursiveScanning(urlBaseFromGlob(*this), *this, urls);
    return urls;
}

URL URL::urlFromPath(std::string_view const path) noexcept
{
    std::string tmp;
    let parts = parse_path(path, tmp);
    return URL(parts);
}

URL URL::urlFromWPath(std::wstring_view const path) noexcept
{
    return urlFromPath(translateString<std::string>(path));
}

URL URL::urlFromExecutableDirectory() noexcept
{
    static auto r = urlFromExecutableFile().urlByRemovingFilename();
    return r;
}

URL URL::urlFromApplicationLogDirectory() noexcept
{
    return urlFromApplicationDataDirectory() / "Log";
}

std::ostream& operator<<(std::ostream& lhs, const URL& rhs)
{
    lhs << rhs.string();
    return lhs;
}

}
