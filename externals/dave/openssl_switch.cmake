# Minimal Horizon adaptations to the pinned OpenSSL source, not global libc
# stubs: Horizon has no POSIX uid/gid/setuid state or posix_memalign symbol.
function(replace_once filename before after)
    file(READ "${SOURCE_DIR}/${filename}" content)
    string(FIND "${content}" "${after}" patched)
    if(NOT patched EQUAL -1)
        return()
    endif()
    string(FIND "${content}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "OpenSSL Switch patch does not match ${filename}")
    endif()
    string(REPLACE "${before}" "${after}" content "${content}")
    file(WRITE "${SOURCE_DIR}/${filename}" "${content}")
endfunction()
replace_once(crypto/uid.c
    "#if defined(OPENSSL_SYS_WIN32) || defined(OPENSSL_SYS_VXWORKS)"
    "#if defined(__SWITCH__) || defined(OPENSSL_SYS_WIN32) || defined(OPENSSL_SYS_VXWORKS)")
replace_once(crypto/mem.c
    "#if defined(_BSD_SOURCE) || (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L)"
    "#if !defined(__SWITCH__) && (defined(_BSD_SOURCE) || (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L))")
