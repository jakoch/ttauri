// Copyright 2019 Pokitec
// All rights reserved.

#include "TTauri/Foundation/FileView.hpp"
#include "TTauri/Foundation/exceptions.hpp"
#include "TTauri/Foundation/logger.hpp"
#include "TTauri/Foundation/memory.hpp"
#include "TTauri/Foundation/URL.hpp"
#include "TTauri/Foundation/required.hpp"
#include <mutex>

namespace TTauri {

std::shared_ptr<FileMapping> FileView::findOrCreateFileMappingObject(URL const& location, AccessMode accessMode, size_t size)
{
    static std::mutex mutex;
    static std::unordered_map<URL, std::vector<std::weak_ptr<FileMapping>>> mappedFileObjects;

    let lock = std::scoped_lock(mutex);

    cleanupWeakPointers(mappedFileObjects);

    auto& mappings = mappedFileObjects[location];

    for (auto weak_fileMappingObject : mappings) {
        if (auto fileMappingObject = weak_fileMappingObject.lock()) {
            if (fileMappingObject->size >= size && fileMappingObject->accessMode() >= accessMode) {
                return fileMappingObject;
            }
        }
    }

    auto fileMappingObject = std::make_shared<FileMapping>(location, accessMode, size);
    mappings.push_back(fileMappingObject);
    return fileMappingObject;
}

}
