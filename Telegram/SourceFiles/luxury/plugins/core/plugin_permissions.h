// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Luxury::Plugins {

enum class Permission : std::uint8_t {
	MessagesRead,
	MessagesSend,
	MessagesModify,
	Ui,
	Network,
	Storage,
	Clipboard,
	TelegramApi,
	TelegramUpdates,
};
inline constexpr auto kPermissionCount = 9;

[[nodiscard]] std::string_view PermissionId(Permission permission);
[[nodiscard]] std::optional<Permission> PermissionFromId(std::string_view id);
[[nodiscard]] bool IsDangerous(Permission permission);

class PermissionSet {
public:
	constexpr PermissionSet() = default;
	constexpr explicit PermissionSet(std::uint32_t bits)
	: _bits(bits & kAllBits) {
	}

	[[nodiscard]] constexpr bool has(Permission permission) const {
		return (_bits & Bit(permission)) != 0;
	}
	constexpr void add(Permission permission) {
		_bits |= Bit(permission);
	}
	constexpr void remove(Permission permission) {
		_bits &= ~Bit(permission);
	}
	[[nodiscard]] constexpr bool contains(PermissionSet other) const {
		return (other._bits & ~_bits) == 0;
	}
	[[nodiscard]] constexpr bool empty() const {
		return !_bits;
	}
	[[nodiscard]] constexpr std::uint32_t bits() const {
		return _bits;
	}
	[[nodiscard]] bool anyDangerous() const;

	// Ids in the order of the Permission enum.
	[[nodiscard]] std::vector<std::string> ids() const;

	friend constexpr bool operator==(PermissionSet, PermissionSet) = default;

private:
	static constexpr std::uint32_t kAllBits = (1U << kPermissionCount) - 1;

	[[nodiscard]] static constexpr std::uint32_t Bit(Permission permission) {
		return 1U << static_cast<std::uint32_t>(permission);
	}

	std::uint32_t _bits = 0;

};

// TL methods refused even with telegram.api, compared case-insensitively:
// auth.*, wallet.*, smsjobs.*, initConnection, every "invoke*" wrapper
// (invokeAfterMsg wraps an arbitrary query and would smuggle a refused
// method through), payments.* except reads, and the account, URL login,
// web app, passkey, push device, payment-details and bot-grant methods
// listed in plugin_permissions.cpp. Stars spent outside payments.* (paid
// messages, suggested posts) are refused by the host by parameter name,
// see Luxury::Plugins::HasPaidParam.
[[nodiscard]] bool IsMethodForbidden(std::string_view tlMethodName);

// Matches a request host against the manifest "network" list.
// The caller passes the bare host: no scheme, no path, port already
// stripped. A host that still contains ':' (an IPv6 literal or an
// unstripped port), '@', '/', '\', whitespace, a control character or an
// empty label is refused. One trailing dot is ignored. "*" matches any
// host, "*.suffix" matches subdomains of suffix but not suffix itself.
// Case-insensitive.
[[nodiscard]] bool HostMatches(
	const std::vector<std::string> &allowed,
	std::string_view host);

} // namespace Luxury::Plugins
