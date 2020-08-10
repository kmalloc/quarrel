#ifndef __QUARREL_STL_H__
#define __QUARREL_STL_H__

#include <memory>

namespace quarrel {

// cast unique_ptr that is created without customized deleter.
template<typename Derived, typename Base>
std::unique_ptr<Derived>
dynamic_unique_ptr_cast_nodel(std::unique_ptr<Base> p) {
    if(Derived *result = dynamic_cast<Derived *>(p.get())) {
        p.release();
        return std::unique_ptr<Derived>(result);
    }
    return std::unique_ptr<Derived>(nullptr);
}

}

#endif
