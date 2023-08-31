#pragma once

#include <tt_eager/tensor/tensor.hpp>
#include "tt_dnn/op_library/operation.hpp"

namespace tt {

namespace tt_metal {

#ifdef DEBUG

namespace operation_history {

struct TensorRecord {
    const StorageType storage_type;
    const Shape shape;
    const DataType data_type;
    const Layout layout;
    const std::optional<MemoryConfig> memory_config;
    tt::stl::reflection::Attributes attributes() const;
};

struct OperationRecord {
    const std::string opcode;
    const tt::stl::reflection::Attributes attributes;
    const std::vector<TensorRecord> input_tensor_records;
    const std::vector<const char*> composite_parent_names{};
};

namespace detail {

struct OperationHistory {

    ~OperationHistory();

    void append(OperationRecord&& record);
    void dump_to_csv();

  private:
    std::vector<OperationRecord> records;
};

inline OperationHistory OPERATION_HISTORY{};

}

template<typename ... Args>
inline void append(Args&& ... args) {
    detail::OPERATION_HISTORY.append(std::forward<Args>(args)...);
}

const char* csv_file_name();

bool enabled();

}  // namespace operation_history

#endif

}  // namespace tt_metal

}  // namespace tt