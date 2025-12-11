#pragma once

#include <capybara/capybara.h>
#include <capybara/runtime.h>

CapyDomain* capy_jit_init(const char* domainName)
{
    // Initialize Capy
    capy_init();

    // Create the Domain
    CapyDomain* cd = capy_init_domain(domainName);

    // Have JIT store this as the Root Domain
    // this should automatically call capy_reload_libraries_into_domain(StoredDomain);

    return cd;

}
