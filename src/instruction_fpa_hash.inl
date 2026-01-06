namespace std
{

template <>
struct hash<INSTRUCTION::fpa_type>
{
    using value_type = INSTRUCTION::fpa_type;

    size_t
    operator()(const value_type& x) const
    {
        const auto& words = x.get_words();
        uint64_t out = std::reduce(words.begin(), words.end(), uint64_t{0},
                            [] (uint64_t acc, uint64_t word) { return acc ^ word; });
        return out;
    }
};

} // std
