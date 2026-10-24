"""luxurygram://chat?id= resolves only chats and channels, never a user with the same bare id.

Source contract on the actual handler; no MTP or native link activation.
"""
from library_regressions import function, source


def check():
    text = source('Telegram/SourceFiles/luxury/luxury_url_handlers.cpp')
    chat = function(text, 'bool ResolveChat(')
    assert 'peerLoaded(' not in chat, 'chat?id= must not look the bare id up as a user peer'
    assert 'searchChatById(' in chat
    assert 'luxury_ChatNotFoundMessage' in chat and 'luxury_UserNotFoundMessage' not in chat
    user = function(text, 'bool ResolveUser(')
    assert 'searchUserById(' in user and 'luxury_UserNotFoundMessage' in user
    strings = source('Telegram/Resources/langs/lang.strings')
    assert '"luxury_ChatNotFoundMessage" = "Chat not found.";' in strings
    overlay = source('Telegram/SourceFiles/luxury/luxury_settings.cpp')
    assert '{ "luxury_ChatNotFoundMessage", "Чат не найден." },' in overlay
    helpers = source('Telegram/SourceFiles/luxury/utils/telegram_helpers.cpp')
    search = function(helpers, 'void searchChatById(')
    assert search.index('channelLoaded(chatId)') < search.index('chatLoaded(chatId)') < search.index('searchPeer(')
    print('chat?id= link contract PASS: channel/chat lookup only, no user-typed peerLoaded, "Chat not found" RU/EN; source-only, not native link activation')


if __name__ == '__main__':
    check()
