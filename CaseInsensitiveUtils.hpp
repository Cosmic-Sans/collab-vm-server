#pragma once

#include <boost/algorithm/string/predicate.hpp>
#include <boost/functional/hash.hpp>

// Based on an example from Boost Functional
struct CaseInsensitiveComparator
{
    CaseInsensitiveComparator() {}
    explicit CaseInsensitiveComparator(std::locale const& l) : locale_(l) {}

    template <typename String1, typename String2>
    bool operator()(String1 const& x1, String2 const& x2) const
    {
        return boost::algorithm::iequals(x1, x2, locale_);
    }
private:
    std::locale locale_;
};

struct CaseInsensitiveHasher
{
    CaseInsensitiveHasher() {}
    explicit CaseInsensitiveHasher(std::locale const& l) : locale_(l) {}

    template <typename String>
    std::size_t operator()(String const& x) const
    {
        std::size_t seed = 0;

        for (typename String::const_iterator it = x.begin();
            it != x.end(); ++it)
        {
            boost::hash_combine(seed, std::toupper(*it, locale_));
        }

        return seed;
    }
private:
    std::locale locale_;
};
