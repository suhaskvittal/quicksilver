/*
 *  author: Suhas Vittal
 *  date:   7 March 2026
 * */

#define TEMPL_PARAMS template <class T, size_t N>
#define TEMPL_CLASS  small_vector<T,N>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS
TEMPL_CLASS::small_vector(size_t s)
    :size_(s)
{
    if (is_heap())
        heap_ = new T[size_];
}

TEMPL_PARAMS
TEMPL_CLASS::small_vector(std::initializer_list<T> il)
    :small_vector(il.begin(), il.end())
{}

TEMPL_PARAMS
TEMPL_CLASS::small_vector(const small_vector& other)
    :size_(other.size_)
{
    if (is_heap())
    {
        heap_ = new T[size_];
        std::memcpy(heap_, other.heap_, size_ * sizeof(T));
    }
    else
    {
        std::memcpy(inline_, other.inline_, size_ * sizeof(T));
    }
}

TEMPL_PARAMS template <class ITER>
TEMPL_CLASS::small_vector(ITER begin, ITER end)
    :size_(std::distance(begin, end))
{
    if (is_heap())
    {
        heap_ = new T[size_];
        std::copy(begin, end, heap_);
    }
    else
    {
        std::copy(begin, end, inline_);
    }
}

TEMPL_PARAMS TEMPL_CLASS&
TEMPL_CLASS::operator=(const small_vector& other)
{
    if (this == &other)
        return *this;
    if (is_heap())
        delete[] heap_;
    size_ = other.size_;
    if (is_heap())
    {
        heap_ = new T[size_];
        std::memcpy(heap_, other.heap_, size_ * sizeof(T));
    }
    else
    {
        std::memcpy(inline_, other.inline_, size_ * sizeof(T));
    }
    return *this;
}

TEMPL_PARAMS
TEMPL_CLASS::~small_vector()
{
    if (is_heap())
        delete[] heap_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
