OWNER(g:yandex_io)

LIBRARY()

SRCS(
    i_mics_state_listener.cpp
    mics_state_provider.cpp
)

END()

RECURSE(
    callback_listener
    file_provider
    hw_provider
    metrica_listener
    sdk_notifier
)
