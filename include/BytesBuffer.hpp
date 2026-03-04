#ifndef BYTES_BUFFER_HPP
#define BYTES_BUFFER_HPP

#include <cstddef>
#include <string_view>
#include <stdexcept>
#include <vector>

struct BytesConstView {
    char const *m_buffer;
    size_t m_size;

    char const *data() const noexcept {
        return m_buffer;
    }

    size_t size() const noexcept {
        return m_size;
    }

    char const *begin() const noexcept {
        return data();
    }

    char const *end() const noexcept {
        return data() + size();
    }

    BytesConstView subspan(size_t start,
                             size_t len = static_cast<size_t>(-1)) const {
        if (start > size()) {
            throw std::out_of_range("BytesConstView::subspan");
        }
        if (len > size() - start) {
            len = size() - start;
        }
        return {data() + start, len};
    }

    operator std::string_view() const noexcept {
        return std::string_view{data(), size()};
    }
};

struct BytesView {
    char *m_buffer;
    size_t m_size;

    char *data() const noexcept {
        return m_buffer;
    }

    size_t size() const noexcept {
        return m_size;
    }

    char *begin() const noexcept {
        return data();
    }

    char *end() const noexcept {
        return data() + size();
    }

    BytesView subspan(size_t start, size_t len) const {
        if (start > size()) {
            throw std::out_of_range("BytesView::subspan");
        }
        if (len > size() - start) {
            len = size() - start;
        }
        return {data() + start, len};
    }

    operator BytesConstView() const noexcept {
        return BytesConstView{data(), size()};
    }

    operator std::string_view() const noexcept {
        return std::string_view{data(), size()};
    }
};


struct BytesBuffer{
    std::vector<char> m_buffer;

    BytesBuffer() = default;
    explicit BytesBuffer(const BytesBuffer&) = default;
    BytesBuffer &operator=(BytesBuffer&&) = default;
    BytesBuffer(BytesBuffer&&) = default;
    BytesBuffer(const std::string &str){
        for(auto s : str){
            m_buffer.emplace_back(s);
        }
    }
    explicit BytesBuffer(size_t n) : m_buffer(n){}

    char const *data() const noexcept {
        return m_buffer.data();
    }

    char *data() noexcept {
        return m_buffer.data();
    }

    size_t size() const noexcept {
        return m_buffer.size();
    }

    char const *begin() const noexcept {
        return data();
    }

    char *begin() noexcept {
        return data();
    }

    char const *end() const noexcept {
        return data() + size();
    }

    char *end() noexcept {
        return data() + size();
    }

    BytesConstView subspan(size_t start, size_t len) const {
        return operator BytesConstView().subspan(start, len);
    }

    BytesView subspan(size_t start, size_t len) {
        return operator BytesView().subspan(start, len);
    }

    operator BytesConstView() const noexcept {
        return BytesConstView{m_buffer.data(), m_buffer.size()};
    }

    operator BytesView() noexcept {
        return BytesView{m_buffer.data(), m_buffer.size()};
    }

    operator std::string_view() const noexcept {
        return std::string_view{m_buffer.data(), m_buffer.size()};
    }

    void append(BytesConstView chunk) {
        m_buffer.insert(m_buffer.end(), chunk.begin(), chunk.end());
    }

    void append(std::string_view chunk) {
        m_buffer.insert(m_buffer.end(), chunk.begin(), chunk.end());
    }

    template <size_t N>
    void append_literial(char const (&literial)[N]) {
        append(std::string_view{literial, N - 1});
    }

    void clear() {
        m_buffer.clear();
    }

    void resize(size_t n) {
        m_buffer.resize(n);
    }

    void reserve(size_t n) {
        m_buffer.reserve(n);
    }
};

#endif