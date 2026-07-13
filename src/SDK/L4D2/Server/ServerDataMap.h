#pragma once

#include <cstddef>

class CBaseEntity;

enum fieldtype_t
{
    FIELD_VOID = 0,
    FIELD_FLOAT,
    FIELD_STRING,
    FIELD_VECTOR,
    FIELD_QUATERNION,
    FIELD_INTEGER,
    FIELD_BOOLEAN,
    FIELD_SHORT,
    FIELD_CHARACTER,
    FIELD_COLOR32,
    FIELD_EMBEDDED,
    FIELD_CUSTOM,
    FIELD_CLASSPTR,
    FIELD_EHANDLE,
    FIELD_EDICT,
    FIELD_POSITION_VECTOR,
    FIELD_TIME,
    FIELD_TICK,
    FIELD_MODELNAME,
    FIELD_SOUNDNAME,
    FIELD_INPUT,
    FIELD_FUNCTION,
    FIELD_VMATRIX,
    FIELD_VMATRIX_WORLDSPACE,
    FIELD_MATRIX3X4_WORLDSPACE,
    FIELD_INTERVAL,
    FIELD_MODELINDEX,
    FIELD_MATERIALINDEX,
    FIELD_VECTOR2D,
    FIELD_TYPECOUNT,
};

enum
{
    TD_OFFSET_NORMAL = 0,
    TD_OFFSET_PACKED = 1,
    TD_OFFSET_COUNT,
};

struct datamap_t;

struct typedescription_t
{
    fieldtype_t fieldType;
    const char* fieldName;
    int fieldOffset[TD_OFFSET_COUNT];
    unsigned short fieldSize;
    short flags;
    const char* externalName;
    void* pSaveRestoreOps;
    void* inputFunc;
    datamap_t* td;
    int fieldSizeInBytes;
    typedescription_t* override_field;
    int override_count;
    float fieldTolerance;
};

struct datamap_t
{
    typedescription_t* dataDesc;
    int dataNumFields;
    const char* dataClassName;
    datamap_t* baseMap;
    bool chains_validated;
    bool packed_offsets_computed;
    int packed_size;
};

namespace L4D2::ServerDataMap
{
    inline constexpr int kGetDataDescMapVTableIndex = 11;

    struct FieldInfo
    {
        typedescription_t* typedesc = nullptr;
        datamap_t* ownerMap = nullptr;
        int actualOffset = 0;
        int depth = 0;
        const char* fieldName = nullptr;
        const char* ownerClassName = nullptr;
        fieldtype_t fieldType = FIELD_VOID;
        unsigned short fieldSize = 0;
        int fieldSizeInBytes = 0;
    };

    struct MoveTypeProbe
    {
        bool ok = false;
        const char* reason = "not-run";
        CBaseEntity* entity = nullptr;
        datamap_t* dataMap = nullptr;
        const char* rootClassName = nullptr;
        FieldInfo field;
        int valueInt = 0;
        unsigned char valueByte = 0;
    };

    datamap_t* GetDataDescMap(CBaseEntity* entity);
    bool FindField(datamap_t* map, const char* fieldName, FieldInfo* outInfo);
    bool TryReadFieldInt(CBaseEntity* entity, const FieldInfo& info, int* outValue);
    bool TryWriteFieldInt(CBaseEntity* entity, const FieldInfo& info, int value);
    MoveTypeProbe ProbeMoveType(CBaseEntity* entity);
}
