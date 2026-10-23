#include "PinGate.h"
#include "BleManager/BleManager.h"
#include <cstdio>
#include <cstring>

bool PinGate::Check(const char* entered) const
{
    if (entered == nullptr) return false;

    // A gateway paired before its install code was kept has none on record, and
    // the gate falls back to the unpaired code until it is paired again.
    char code[Len + 1] = {};
    if (!serviceProvider_.getBleManager().GetInstallCode(code, sizeof(code)))
        snprintf(code, sizeof(code), "%s", kUnpairedCode);
    return std::strcmp(entered, code) == 0;
}

bool PinGate::UsesInstallCode() const
{
    char code[Len + 1];
    return serviceProvider_.getBleManager().GetInstallCode(code, sizeof(code));
}

bool PinGate::Owner(char* name, size_t nameCap, char* id, size_t idCap) const
{
    name[0] = '\0';
    id[0] = '\0';

    BleManager::Peer paired;
    if (!serviceProvider_.getBleManager().GetPaired(paired)) return false;

    snprintf(name, nameCap, "%s", paired.name);
    if (paired.hasDgid && paired.dgid != 0)
        snprintf(id, idCap, "id %lu", static_cast<unsigned long>(paired.dgid));
    else if (name[0] == '\0')
        BleManager::FormatAddr(paired.addr, name, nameCap);   // all it gave
    return true;
}
