/*
 *  author: Suhas Vittal
 *  date:   7 March 2026
 * */

#define TEMPL_PARAMS template <class T, size_t N>
#define TemplClass  SmallVector<T,N>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS
TemplClass::SmallVector(size_t s)
    :size_(s)
{
    if (is_heap())
        heap_ = new T[size_];
}

TEMPL_PARAMS
TemplClass::SmallVector(std::initializer_list<T> il)
    :SmallVector(il.begin(), il.end())
{}

TEMPL_PARAMS
TemplClass::SmallVector(const SmallVector& other)
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

TEMPL_PARAMS template <class Iter>
TemplClass::SmallVector(Iter begin, Iter end)
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

TEMPL_PARAMS TemplClass&
TemplClass::operator=(const SmallVector& other)
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
TemplClass::~SmallVector()
{
    if (is_heap())
        delete[] heap_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#undef TEMPL_PARAMS
#undef TemplClass
