// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
namespace tube5
{
class NativeFramebuffer final
{
  public:
    ~NativeFramebuffer();
    NativeFramebuffer() = default;
    NativeFramebuffer(const NativeFramebuffer &) = delete;
    NativeFramebuffer &operator=(const NativeFramebuffer &) = delete;
    int allocate(std::size_t bytes, std::size_t alignment = 0x200000) noexcept;
    int release() noexcept;
    void *data() const noexcept
    {
        return mapped_;
    }

  private:
    void *mapped_ = nullptr;
    std::int64_t physical_ = 0;
    std::size_t bytes_ = 0;
};
} // namespace tube5
