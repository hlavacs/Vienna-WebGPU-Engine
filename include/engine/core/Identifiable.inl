#pragma once

#include <utility>

namespace engine::core {

template<typename T>
Identifiable<T>::Identifiable(std::optional<std::string> name)
    : m_id(IDGenerator::nextID()), m_name(std::move(name)) {}

template<typename T>
Handle<T> Identifiable<T>::getHandle() const {
    return Handle<T>(m_id);
}

template<typename T>
std::optional<std::string> Identifiable<T>::getName() const {
    std::lock_guard<std::mutex> lock(m_nameMutex);
    return m_name;
}

template<typename T>
void Identifiable<T>::setName(std::string newName) {
    std::lock_guard<std::mutex> lock(m_nameMutex);
    m_name = std::move(newName);
}

} // namespace engine::core
