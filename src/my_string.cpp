// ============================================================================
// 作业 2：String 类的实现文件
//
// 实现 include/my_string.h 中声明的所有成员函数与运算符。
// 内部表示：char* data_ + size_ + capacity_，缓冲区始终以 '\0' 结尾，
// capacity_ 不含结尾 '\0'（实际分配 capacity_ + 1 字节）。
// ============================================================================

#include "my_string.h"

#include <istream>
#include <ostream>
#include <stdexcept>
#include <utility>  // std::move

namespace {

// 默认构造的空串容量契约：不低于 16（不含结尾 '\0'）。
constexpr std::size_t kMinCapacity = 16;

// 手动计算 C 字符串长度（不使用 <cstring> 的 strlen）。
std::size_t raw_length(const char* s) noexcept {
    std::size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

// 正向逐字节复制（memcpy 语义：src、dst 不重叠）。
void raw_copy(char* dst, const char* src, std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

// 允许重叠的移动（memmove 语义）。
// dst < src 时从前往后复制；dst > src 时从后往前复制。
void raw_move(char* dst, const char* src, std::size_t count) noexcept {
    if (dst == src || count == 0) {
        return;
    }
    if (dst < src) {
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = src[i];
        }
    } else {
        // 从后往前，避免覆盖尚未搬走的源数据
        for (std::size_t i = count; i > 0; --i) {
            dst[i - 1] = src[i - 1];
        }
    }
}

// 扩容策略：max(所需长度, 当前容量 x 2, kMinCapacity)，
// 保证连续 push_back 的摊销复杂度为 O(1)。
std::size_t growth_target(std::size_t current_capacity, std::size_t needed) noexcept {
    std::size_t target = current_capacity * 2;
    if (target < needed) {
        target = needed;
    }
    if (target < kMinCapacity) {
        target = kMinCapacity;
    }
    return target;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

String::String()
    : data_(new char[kMinCapacity + 1]), size_(0), capacity_(kMinCapacity) {
    data_[0] = '\0';
}

String::String(const char* str)
    : data_(nullptr), size_(0), capacity_(0) {
    // nullptr 视为空串，不解引用。
    const char* safe = (str != nullptr) ? str : "";
    const std::size_t len = raw_length(safe);
    const std::size_t cap = (len > kMinCapacity) ? len : kMinCapacity;

    // 先分配成功，再写成员：分配失败时对象仍是可析构的空状态。
    char* fresh = new char[cap + 1];
    raw_copy(fresh, safe, len);
    fresh[len] = '\0';

    data_ = fresh;
    size_ = len;
    capacity_ = cap;
}

String::~String() {
    delete[] data_;  // delete[] nullptr 合法
}

// ---------------------------------------------------------------------------
// 拷贝构造（深拷贝）
// ---------------------------------------------------------------------------

String::String(const String& other)
    : data_(nullptr), size_(0), capacity_(0) {
    const std::size_t len = other.size_;
    std::size_t cap = (len > kMinCapacity) ? len : kMinCapacity;
    if (cap < other.capacity_) {
        // 保留与源对象相同的容量（不小于 len，也不小于 kMinCapacity）
        cap = other.capacity_;
    }

    char* fresh = new char[cap + 1];
    if (len > 0) {
        raw_copy(fresh, other.data_, len);
    }
    fresh[len] = '\0';

    data_ = fresh;
    size_ = len;
    capacity_ = cap;
}

// ---------------------------------------------------------------------------
// 移动构造（窃取缓冲区）
// ---------------------------------------------------------------------------

String::String(String&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    // 源对象进入"有效但内容未指定"状态：置空，析构时安全。
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

// ---------------------------------------------------------------------------
// 复制赋值（copy-and-swap，自赋值安全，强异常安全）
// ---------------------------------------------------------------------------

String& String::operator=(const String& other) {
    if (this == &other) {
        return *this;
    }
    String tmp(other);  // 先深拷贝（可能抛 bad_alloc），此时 *this 未变
    swap(tmp);          // 成功后才接管；tmp 析构时释放原来的缓冲区
    return *this;
}

// ---------------------------------------------------------------------------
// 移动赋值（自移动安全）
// ---------------------------------------------------------------------------

String& String::operator=(String&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    delete[] data_;  // 释放自己原来的缓冲区

    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;

    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    return *this;
}

// ---------------------------------------------------------------------------
// 拼接：返回新对象，不修改操作数
// ---------------------------------------------------------------------------

String String::operator+(const String& other) const {
    String result;  // 空串，容量 kMinCapacity
    const std::size_t total = size_ + other.size_;
    result.ensure_capacity(total);

    if (size_ > 0) {
        raw_copy(result.data_, data_, size_);
    }
    if (other.size_ > 0) {
        raw_copy(result.data_ + size_, other.data_, other.size_);
    }
    result.data_[total] = '\0';
    result.size_ = total;
    return result;
}

// ---------------------------------------------------------------------------
// 下标访问：不做边界检查（与 std::string 一致）
// index == size() 合法，返回结尾 '\0' 的引用
// ---------------------------------------------------------------------------

char& String::operator[](std::size_t index) noexcept {
    return data_[index];
}

const char& String::operator[](std::size_t index) const noexcept {
    return data_[index];
}

// ---------------------------------------------------------------------------
// 带边界检查的访问：index >= size() 抛出 std::out_of_range
// ---------------------------------------------------------------------------

char& String::at(std::size_t index) {
    if (index >= size_) {
        throw std::out_of_range("String::at: index out of range");
    }
    return data_[index];
}

const char& String::at(std::size_t index) const {
    if (index >= size_) {
        throw std::out_of_range("String::at: index out of range");
    }
    return data_[index];
}

// ---------------------------------------------------------------------------
// 长度 / 容量
// ---------------------------------------------------------------------------

std::size_t String::size() const noexcept {
    return size_;
}

std::size_t String::capacity() const noexcept {
    return capacity_;
}

// ---------------------------------------------------------------------------
// 扩容：先分配成功，再释放旧的（强异常安全）
// ---------------------------------------------------------------------------

void String::ensure_capacity(std::size_t needed) {
    if (needed <= capacity_) {
        return;
    }
    const std::size_t target = growth_target(capacity_, needed);
    char* fresh = new char[target + 1];  // 失败则抛出，对象未被改动
    if (size_ > 0) {
        raw_copy(fresh, data_, size_);
    }
    fresh[size_] = '\0';

    delete[] data_;  // 成功后才释放旧缓冲区
    data_ = fresh;
    capacity_ = target;
}

// ---------------------------------------------------------------------------
// 在 pos 处插入 str
// pos > size() 抛出 std::out_of_range；自插入安全；强异常安全
// ---------------------------------------------------------------------------

void String::insert(std::size_t pos, const String& str) {
    if (pos > size_) {
        throw std::out_of_range("String::insert: pos out of range");
    }
    const std::size_t added = str.size_;
    if (added == 0) {
        return;  // 插入空串（包括自插入空串）什么都不做
    }

    const std::size_t new_size = size_ + added;

    if (new_size > capacity_) {
        // 情况 A：容量不足。在新缓冲区里一次性组装，最后才释放旧缓冲区。
        // 自插入（str 就是 *this）也安全：str.data_ 仍指向旧缓冲区，
        // 只要把 delete[] data_ 放在最后，读取旧数据就没问题。
        const std::size_t target = growth_target(capacity_, new_size);
        char* fresh = new char[target + 1];

        // [0, pos)
        if (pos > 0) {
            raw_copy(fresh, data_, pos);
        }
        // [pos, pos + added)  ← str 的内容
        raw_copy(fresh + pos, str.data_, added);
        // [pos + added, new_size)  ← 原 [pos, size_) 的内容
        if (size_ > pos) {
            raw_copy(fresh + pos + added, data_ + pos, size_ - pos);
        }
        fresh[new_size] = '\0';

        delete[] data_;
        data_ = fresh;
        capacity_ = target;
    } else {
        // 情况 B：容量足够。原地腾位置。
        // 1. 把 [pos, size_]（含结尾 '\0'）整体后移 added 字节。
        //    必须用允许重叠的移动（memmove 语义），从后往前复制。
        raw_move(data_ + pos + added, data_ + pos, size_ - pos + 1);
        // 2. 写入 str 的内容到 [pos, pos + added)。
        //    自插入时 str.data_ == data_，源区间 [0, added) 与目标区间
        //    [pos, pos + added) 可能重叠（当 pos < added 时），因此必须用
        //    支持重叠的 raw_move（memmove 语义）而非 raw_copy。
        raw_move(data_ + pos, str.data_, added);
        // 注意：'\0' 已在第 1 步随原结尾一起移到了 new_size 处。
    }

    size_ = new_size;
}

// ---------------------------------------------------------------------------
// 末尾追加一个字符，容量不足时自动扩容
// ---------------------------------------------------------------------------

void String::push_back(char ch) {
    ensure_capacity(size_ + 1);  // 可能抛出；抛出时对象保持不变
    data_[size_] = ch;
    ++size_;
    data_[size_] = '\0';
}

// ---------------------------------------------------------------------------
// 转换为 C 字符串
// 即使对象被移动过（data_ == nullptr）也必须返回有效的以 '\0' 结尾的指针
// ---------------------------------------------------------------------------

const char* String::c_str() const noexcept {
    return data_ != nullptr ? data_ : "";
}

String::operator const char*() const noexcept {
    return c_str();
}

// ---------------------------------------------------------------------------
// 交换全部内容（自交换天然安全）
// ---------------------------------------------------------------------------

void String::swap(String& other) noexcept {
    char* tmp_data = data_;
    data_ = other.data_;
    other.data_ = tmp_data;

    std::size_t tmp_size = size_;
    size_ = other.size_;
    other.size_ = tmp_size;

    std::size_t tmp_cap = capacity_;
    capacity_ = other.capacity_;
    other.capacity_ = tmp_cap;
}

// ---------------------------------------------------------------------------
// 流运算符（bonus）
// ---------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const String& str) {
    if (str.data_ != nullptr && str.size_ > 0) {
        os.write(str.data_, static_cast<std::streamsize>(str.size_));
    }
    return os;
}

std::istream& operator>>(std::istream& is, String& str) {
    std::istream::sentry sentry(is);  // 跳过前导空白并检查流状态
    if (!sentry) {
        return is;
    }

    String temp;  // 先读到临时对象，全部成功后再提交给 str
    bool got = false;
    for (;;) {
        const std::istream::int_type next = is.get();
        if (next == std::istream::traits_type::eof()) {
            break;
        }
        const char ch = static_cast<char>(next);
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\v' || ch == '\f' ||
            ch == '\r') {
            is.unget();  // 空白不属于结果，退还给下一次读取
            break;
        }
        temp.push_back(ch);
        got = true;
    }

    if (got) {
        str = std::move(temp);  // 提交
    } else {
        is.setstate(std::ios_base::failbit);  // 没读到任何字符；str 保持原值
    }
    return is;
}
