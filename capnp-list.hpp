// Cap'n Proto appears to have support for compatiblity with STL iterators
// by defining the macro KJ_STD_COMPAT, but it doesn't work with MSVC unless
// this specialization is defined.
//#ifdef WIN32
////#include <kj/windows-sanity.h>
//#undef VOID
//#undef CONST
//#undef SendMessage
//#endif
//#include <capnp/common.h>
#include <iterator>
#include <capnp/list.h>
namespace std 
{
  template <typename Container, typename Element>
  struct iterator_traits<capnp::_::IndexingIterator<Container, Element>>
  {
    using iterator_category = random_access_iterator_tag;
    using value_type = Element;
    using difference_type = int;

    using pointer = Element*;
    using reference = Element&;
  };
} // namespace std
