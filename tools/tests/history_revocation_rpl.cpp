// Validate the RPL revocation sequence used by HistoryTextViewer.
#include <rpl/rpl.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <optional>
#include <vector>

// The standalone test does not initialize the application's assertion logger.
void base::assertion::log(const char *message, const char *file, int line) {
	std::fprintf(stderr, "%s %s:%d\n", message, file, line);
}

int main() {
	for (int cause = 0; cause != 5; ++cause) {
		auto locks = std::array<rpl::variable<bool>, 4>();
		auto active = rpl::variable<bool>(true);
		auto source = rpl::event_stream<int>();
		auto updates = std::vector<std::optional<int>>();
		auto lifetime = rpl::lifetime();
		rpl::combine(
			locks[0].value(), locks[1].value(), locks[2].value(),
			locks[3].value(), active.value(),
			[](bool a, bool b, bool c, bool d, bool session) {
				return !a && !b && !c && !d && session;
			}
		) | rpl::distinct_until_changed()
		  | rpl::take_while(rpl::mappers::_1)
		  | rpl::then(rpl::single(false))
		  | rpl::map([&](bool allowed) -> rpl::producer<std::optional<int>> {
			if (!allowed) return rpl::single(std::optional<int>());
			return source.events() | rpl::map([](int value) {
				return std::optional<int>(value);
			});
		  }) | rpl::flatten_latest()
		  | rpl::on_next([&](std::optional<int> value) {
			updates.push_back(value);
		  }, lifetime);
		source.fire(1);
		if (cause == 4) active = false;
		else locks[cause] = true;
		source.fire(2);
		if (cause == 4) active = true;
		else locks[cause] = false;
		source.fire(3);
		assert((updates == std::vector<std::optional<int>>{ 1, std::nullopt }));
	}
}
