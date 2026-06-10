#pragma once

#include <vector>

template <typename T> class Texture2D {
private:
  int width, height;
  std::vector<T> data;

public:
  Texture2D(int w, int h, T initialValue)
      : width(w), height(h), data(w * h, initialValue) {}

  void resize(int w, int h, T initialValue) {
    width = w;
    height = h;
    data.assign(w * h, initialValue);
  }

  void clear(T value) { std::fill(data.begin(), data.end(), value); }

  T &at(int x, int y) { return data[y * width + x]; }
  const T &at(int x, int y) const { return data[y * width + x]; }

  int getWidth() const { return width; }
  int getHeight() const { return height; }
  T *getData() { return data.data(); }
};
