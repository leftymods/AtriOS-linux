function atomicType(arg) {
    switch (arg.name) {
        case 'data40':
        case 'data48':
        case 'data56':
        case 'data64':
            return 'uint64_t'
        case 'bool':
            return 'bool'
        case 'map8':
            return 'uint8_t'
        case 'map16':
            return 'uint16_t'
        case 'map24': 
        case 'map32': 
            return 'uint32_t'
        case 'map40': 
        case 'map48': 
        case 'map56': 
        case 'map64': 
            return 'uint64_t'
        case 'uint8': 
            return 'uint8_t'
        case 'uint16':
            return 'uint16_t'
        case 'uint24':
        case 'uint32':
            return 'uint32_t'
        case 'uint40':
        case 'uint48':
        case 'uint56':
        case 'uint64':
            return 'uint64_t'
        case 'int8':
            return 'int8_t'
        case 'int16':
            return 'int16_t'
        case 'int24':
        case 'int32':
            return 'int32_t'
        case 'int40':
        case 'int48':
        case 'int56':
        case 'int64':
            return 'int64_t'
        case 'semi':
        case 'single':
            return 'float'
        case 'double':
            return 'double'
        case 'octstr':
        case 'string':
        case 'octstr16':
        case 'string16':
            return 'std::string'
        case 'struct':
            return 'std::vector<AnyType>'
        case 'ToD':
        case 'date':
        case 'UTC':
            return 'uint32_t'
        case 'clusterId':
        case 'attribId':
            return 'uint16_t'
        case 'bacOID':
            return 'uint32_t'
        case 'EUI64':
            return 'std::array<uint8_t, 8>'
        case 'key128':
            return 'std::array<uint8_t, 16>'
        case 'unk':
            return 'std::any'
        default:
            throw "no override"
    }
}

exports.atomicType = atomicType
