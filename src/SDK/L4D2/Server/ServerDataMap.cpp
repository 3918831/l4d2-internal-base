#include "ServerDataMap.h"

#include <Windows.h>
#include <cstring>

#include "../Includes/iserverunknown.h"

namespace
{
    constexpr int kMaxDataMapDepth = 16;
    constexpr int kMaxDataMapFields = 4096;
    constexpr size_t kMaxSafeString = 128;

    bool IsReadableRange(const void* address, size_t size)
    {
        if (!address || size == 0)
            return false;

        const unsigned char* current = static_cast<const unsigned char*>(address);
        size_t remaining = size;

        while (remaining > 0)
        {
            MEMORY_BASIC_INFORMATION mbi = {};
            if (VirtualQuery(current, &mbi, sizeof(mbi)) == 0)
                return false;

            if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_NOACCESS) || (mbi.Protect & PAGE_GUARD))
                return false;

            const size_t regionLeft = static_cast<const unsigned char*>(mbi.BaseAddress) + mbi.RegionSize - current;
            if (regionLeft == 0)
                return false;

            if (regionLeft >= remaining)
                return true;

            remaining -= regionLeft;
            current += regionLeft;
        }

        return true;
    }

    bool IsWritableRange(void* address, size_t size)
    {
        if (!address || size == 0)
            return false;

        unsigned char* current = static_cast<unsigned char*>(address);
        size_t remaining = size;

        while (remaining > 0)
        {
            MEMORY_BASIC_INFORMATION mbi = {};
            if (VirtualQuery(current, &mbi, sizeof(mbi)) == 0)
                return false;

            if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_NOACCESS) || (mbi.Protect & PAGE_GUARD))
                return false;

            constexpr DWORD writable =
                PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            if ((mbi.Protect & writable) == 0)
                return false;

            const size_t regionLeft = static_cast<unsigned char*>(mbi.BaseAddress) + mbi.RegionSize - current;
            if (regionLeft == 0)
                return false;

            if (regionLeft >= remaining)
                return true;

            remaining -= regionLeft;
            current += regionLeft;
        }

        return true;
    }

    bool IsExecutablePointer(const void* address)
    {
        if (!address)
            return false;

        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
            return false;

        if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS))
            return false;

        constexpr DWORD executable =
            PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (mbi.Protect & executable) != 0;
    }

    bool IsSafeString(const char* text)
    {
        if (!text || !IsReadableRange(text, 1))
            return false;

        for (size_t i = 0; i < kMaxSafeString; ++i)
        {
            if (!IsReadableRange(text + i, 1))
                return false;
            if (text[i] == '\0')
                return true;
        }

        return false;
    }

    int FieldOffset(const typedescription_t& typedesc)
    {
        return typedesc.fieldOffset[TD_OFFSET_NORMAL];
    }

    bool ValidateDataMap(datamap_t* map)
    {
        if (!map || !IsReadableRange(map, sizeof(datamap_t)))
            return false;

        if (map->dataNumFields < 0 || map->dataNumFields > kMaxDataMapFields)
            return false;

        if (map->dataNumFields > 0 && !IsReadableRange(map->dataDesc, sizeof(typedescription_t)))
            return false;

        return true;
    }

    bool FindFieldRecursive(datamap_t* map, const char* fieldName, int depth, int baseOffset, L4D2::ServerDataMap::FieldInfo* outInfo)
    {
        if (!fieldName || depth > kMaxDataMapDepth || !ValidateDataMap(map))
            return false;

        for (int i = 0; i < map->dataNumFields; ++i)
        {
            typedescription_t* typedesc = &map->dataDesc[i];
            if (!IsReadableRange(typedesc, sizeof(typedescription_t)))
                return false;

            const char* currentName = typedesc->fieldName;
            if (IsSafeString(currentName) && std::strcmp(fieldName, currentName) == 0)
            {
                if (outInfo)
                {
                    outInfo->typedesc = typedesc;
                    outInfo->ownerMap = map;
                    outInfo->actualOffset = baseOffset + FieldOffset(*typedesc);
                    outInfo->depth = depth;
                    outInfo->fieldName = currentName;
                    outInfo->ownerClassName = IsSafeString(map->dataClassName) ? map->dataClassName : "unknown";
                    outInfo->fieldType = typedesc->fieldType;
                    outInfo->fieldSize = typedesc->fieldSize;
                    outInfo->fieldSizeInBytes = typedesc->fieldSizeInBytes;
                }
                return true;
            }

            if (typedesc->td)
            {
                L4D2::ServerDataMap::FieldInfo nested;
                if (FindFieldRecursive(typedesc->td, fieldName, depth + 1, baseOffset + FieldOffset(*typedesc), &nested))
                {
                    if (outInfo)
                        *outInfo = nested;
                    return true;
                }
            }
        }

        return FindFieldRecursive(map->baseMap, fieldName, depth + 1, baseOffset, outInfo);
    }

    bool ReadFieldValueAsInt(const void* fieldAddress, const typedescription_t* typedesc, int* outValue)
    {
        if (!fieldAddress || !typedesc || !outValue)
            return false;

        switch (typedesc->fieldType)
        {
        case FIELD_BOOLEAN:
        case FIELD_CHARACTER:
            if (!IsReadableRange(fieldAddress, sizeof(unsigned char)))
                return false;
            *outValue = static_cast<int>(*static_cast<const unsigned char*>(fieldAddress));
            return true;

        case FIELD_SHORT:
            if (!IsReadableRange(fieldAddress, sizeof(short)))
                return false;
            *outValue = static_cast<int>(*static_cast<const short*>(fieldAddress));
            return true;

        case FIELD_INTEGER:
            if (!IsReadableRange(fieldAddress, sizeof(int)))
                return false;
            *outValue = *static_cast<const int*>(fieldAddress);
            return true;

        default:
            return false;
        }
    }

    bool WriteFieldValueFromInt(void* fieldAddress, const typedescription_t* typedesc, int value)
    {
        if (!fieldAddress || !typedesc)
            return false;

        switch (typedesc->fieldType)
        {
        case FIELD_BOOLEAN:
        case FIELD_CHARACTER:
            if (!IsWritableRange(fieldAddress, sizeof(unsigned char)))
                return false;
            *static_cast<unsigned char*>(fieldAddress) = static_cast<unsigned char>(value & 0xFF);
            return true;

        case FIELD_SHORT:
            if (!IsWritableRange(fieldAddress, sizeof(short)))
                return false;
            *static_cast<short*>(fieldAddress) = static_cast<short>(value);
            return true;

        case FIELD_INTEGER:
            if (!IsWritableRange(fieldAddress, sizeof(int)))
                return false;
            *static_cast<int*>(fieldAddress) = value;
            return true;

        default:
            return false;
        }
    }
}

namespace L4D2::ServerDataMap
{
    datamap_t* GetDataDescMap(CBaseEntity* entity)
    {
        if (!entity || !IsReadableRange(entity, sizeof(void*)))
            return nullptr;

        void** vtable = *reinterpret_cast<void***>(entity);
        if (!IsReadableRange(vtable, sizeof(void*) * (kGetDataDescMapVTableIndex + 1)))
            return nullptr;

        void* function = vtable[kGetDataDescMapVTableIndex];
        if (!IsExecutablePointer(function))
            return nullptr;

        using GetDataDescMapFn = datamap_t*(__thiscall*)(void*);
        datamap_t* map = nullptr;

        __try
        {
            map = reinterpret_cast<GetDataDescMapFn>(function)(entity);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            map = nullptr;
        }

        return ValidateDataMap(map) ? map : nullptr;
    }

    bool FindField(datamap_t* map, const char* fieldName, FieldInfo* outInfo)
    {
        return FindFieldRecursive(map, fieldName, 0, 0, outInfo);
    }

    bool TryReadFieldInt(CBaseEntity* entity, const FieldInfo& info, int* outValue)
    {
        if (!entity || !info.typedesc || !outValue)
            return false;

        const unsigned char* fieldAddress = reinterpret_cast<const unsigned char*>(entity) + info.actualOffset;
        return ReadFieldValueAsInt(fieldAddress, info.typedesc, outValue);
    }

    bool TryWriteFieldInt(CBaseEntity* entity, const FieldInfo& info, int value)
    {
        if (!entity || !info.typedesc)
            return false;

        unsigned char* fieldAddress = reinterpret_cast<unsigned char*>(entity) + info.actualOffset;
        return WriteFieldValueFromInt(fieldAddress, info.typedesc, value);
    }

    MoveTypeProbe ProbeMoveType(CBaseEntity* entity)
    {
        MoveTypeProbe probe;
        probe.entity = entity;

        if (!entity)
        {
            probe.reason = "missing-entity";
            return probe;
        }

        datamap_t* map = GetDataDescMap(entity);
        probe.dataMap = map;
        if (!map)
        {
            probe.reason = "missing-datamap";
            return probe;
        }

        probe.rootClassName = IsSafeString(map->dataClassName) ? map->dataClassName : "unknown";

        FieldInfo field;
        if (!FindField(map, "m_MoveType", &field))
        {
            probe.reason = "missing-field";
            return probe;
        }

        probe.field = field;

        int value = 0;
        if (!TryReadFieldInt(entity, field, &value))
        {
            probe.reason = "read-failed";
            return probe;
        }

        probe.valueInt = value;
        probe.valueByte = static_cast<unsigned char>(value & 0xFF);
        probe.ok = true;
        probe.reason = "ok";
        return probe;
    }
}
