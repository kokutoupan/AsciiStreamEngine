#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <mdspan>
#include <vector>

template <typename T> class TextureView;

template <typename V, typename ValueType>
concept convertible_to_texture_view =
    std::convertible_to<V, TextureView<ValueType>> ||
    std::convertible_to<V, TextureView<const ValueType>>;

template <typename T> class TextureView {
public:
  using value_type = T;
  // 内部表現としての mdspan 型
  using mdspan_type = std::mdspan<
      T, std::extents<std::size_t, std::dynamic_extent, std::dynamic_extent>,
      std::layout_stride>;

  using mapping_type = typename mdspan_type::mapping_type;
  using extents_type = typename mdspan_type::extents_type;

private:
  mdspan_type m_span;

public:
  constexpr TextureView() noexcept = default;
  constexpr TextureView(mdspan_type span) noexcept : m_span(span) {}

  template <typename U>
    requires std::convertible_to<U *, T *>
  constexpr TextureView(const TextureView<U> &other) noexcept
      : m_span(other.get()) {}

  // 生の mdspan やポインタへのアクセス
  [[nodiscard]] constexpr mdspan_type &get() noexcept { return m_span; }
  [[nodiscard]] constexpr const mdspan_type &get() const noexcept {
    return m_span;
  }
  [[nodiscard]] constexpr T *data() noexcept { return m_span.data_handle(); }
  [[nodiscard]] constexpr const T *data() const noexcept {
    return m_span.data_handle();
  }

  [[nodiscard]] constexpr std::size_t width() const noexcept {
    return m_span.extent(1);
  }
  [[nodiscard]] constexpr std::size_t height() const noexcept {
    return m_span.extent(0);
  }

  // 2次元アクセス [y, x]
  [[nodiscard]] constexpr T &operator[](std::size_t y, std::size_t x) noexcept {
    return m_span[y, x]; // 内部の mdspan も [y, x] で呼ぶ
  }

  [[nodiscard]] constexpr const T &operator[](std::size_t y,
                                              std::size_t x) const noexcept {
    return m_span[y, x];
  }

  [[nodiscard]] constexpr TextureView
  subView(std::size_t x, std::size_t y, std::size_t width,
          std::size_t height) const noexcept {
    assert(x + width <= this->width() && y + height <= this->height());

    mdspan_type local_span = m_span;
    T *sub_data = &local_span[y, x];
    std::size_t orig_stride = local_span.mapping().stride(0);

    auto ext = extents_type(height, width);
    auto map = mapping_type(ext, std::array<std::size_t, 2>{orig_stride, 1});

    return TextureView(mdspan_type(sub_data, map));
  }

  [[nodiscard]] constexpr T &at(std::size_t x, std::size_t y) noexcept {
    return m_span[y, x];
  }

  [[nodiscard]] constexpr const T &at(std::size_t x,
                                      std::size_t y) const noexcept {
    return m_span[y, x];
  }

  [[nodiscard]] constexpr T sampleBilinear(float u, float v) const noexcept {
    u = std::max(0.0f, std::min(1.0f, u));
    v = std::max(0.0f, std::min(1.0f, v));

    float texX = u * (static_cast<float>(width()) - 1.0f);
    float texY = v * (static_cast<float>(height()) - 1.0f);

    int x0 = static_cast<int>(std::floor(texX));
    int y0 = static_cast<int>(std::floor(texY));
    int x1 = std::min(static_cast<int>(width()) - 1, x0 + 1);
    int y1 = std::min(static_cast<int>(height()) - 1, y0 + 1);

    float tx = texX - x0;
    float ty = texY - y0;

    const T &s00 = m_span[y0, x0];
    const T &s10 = m_span[y0, x1];
    const T &s01 = m_span[y1, x0];
    const T &s11 = m_span[y1, x1];

    T s0 = s00 * (1.0f - tx) + s10 * tx;
    T s1 = s01 * (1.0f - tx) + s11 * tx;

    return s0 * (1.0f - ty) + s1 * ty;
  }

  constexpr void clear(T value) noexcept {
    std::size_t h = height();
    std::size_t w = width();
    for (std::size_t y = 0; y < h; ++y) {
      T *row = &m_span[y, 0];
      std::fill_n(row, w, value);
    }
  }
};

template <typename T> class Texture2D {
private:
  int m_width, m_height;
  std::vector<T> m_data;

public:
  Texture2D(int w, int h, T initialValue)
      : m_width(w), m_height(h), m_data(w * h, initialValue) {}

  void resize(int w, int h, T initialValue) {
    m_width = w;
    m_height = h;
    m_data.assign(w * h, initialValue);
  }

  void clear(T value) { std::fill(m_data.begin(), m_data.end(), value); }

  T &at(int x, int y) { return m_data[y * m_width + x]; }
  const T &at(int x, int y) const { return m_data[y * m_width + x]; }

  [[nodiscard]] int width() const { return m_width; }
  [[nodiscard]] int height() const { return m_height; }
  [[nodiscard]] T *data() { return m_data.data(); }
  [[nodiscard]] const T *data() const { return m_data.data(); }
  [[nodiscard]] size_t size() const { return m_data.size(); }

  T sampleBilinear(float u, float v) const {
    return view().sampleBilinear(u, v);
  }

  [[nodiscard]] constexpr TextureView<T> view() noexcept {

    using view_type = TextureView<T>;
    using extents_type = typename view_type::extents_type;
    using mapping_type = typename view_type::mapping_type;
    using mdspan_type = typename view_type::mdspan_type;

    auto ext = extents_type(static_cast<std::size_t>(m_height),
                            static_cast<std::size_t>(m_width));
    auto map = mapping_type(
        ext, std::array<std::size_t, 2>{static_cast<std::size_t>(m_width), 1});

    return view_type(mdspan_type(m_data.data(), map));
  }

  [[nodiscard]] constexpr TextureView<const T> view() const noexcept {

    using view_type = TextureView<const T>;
    using extents_type = typename view_type::extents_type;
    using mapping_type = typename view_type::mapping_type;
    using mdspan_type = typename view_type::mdspan_type;

    auto ext = extents_type(static_cast<std::size_t>(m_height),
                            static_cast<std::size_t>(m_width));
    auto map = mapping_type(
        ext, std::array<std::size_t, 2>{static_cast<std::size_t>(m_width), 1});

    return view_type(mdspan_type(m_data.data(), map));
  }

  constexpr operator TextureView<T>() noexcept { return this->view(); }

  constexpr operator TextureView<const T>() const noexcept {
    return this->view();
  }
};
