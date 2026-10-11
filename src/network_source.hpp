// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace tube5
{
// One session at a time. Reset only after the previous readers have joined.
void begin_network_session() noexcept;
void cancel_network_session() noexcept;
void record_network_read(const void *context, int result, int io_error) noexcept;
int network_playback_error() noexcept;
void finish_network_open() noexcept;
const char *network_open_error() noexcept;
// Custom verified HTTPS AVIO: keep the existing deadline/cancel/read tracking.
void prepare_custom_network(void *format_context) noexcept;
bool network_session_interrupted() noexcept;
} // namespace tube5
