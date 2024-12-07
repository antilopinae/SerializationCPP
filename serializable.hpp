#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Core {

template <typename T>
concept Serializable = std::is_trivially_copyable_v<T> || requires(T& nonConstObj, const uint8_t* data) {
  { nonConstObj.serialize() } -> std::same_as<const uint8_t*>;
  { T::size() } -> std::same_as<uint64_t>;
  { nonConstObj.deserialize(data) } -> std::same_as<bool>;
};

template <typename T>
concept serializable = requires(T& nonConstObj, const uint8_t* data) {
  { nonConstObj.serialize() } -> std::same_as<const uint8_t*>;
  { T::size() } -> std::same_as<uint64_t>;
  { nonConstObj.deserialize(data) } -> std::same_as<bool>;
};

/*
// основная идея: сделать контейнер, который сам сериализует и десериализует
любые тривиально-копируемые, стандартные типы или контейнеры из них, а также
типы,
// которые поддерживают интерфейс Serializable (реализуют методы serialize(),
size(), deserialize())
// контейнер также хранит внутри себя данные всех этих типов и предоставляет к
ним доступ T& get<Index>(), void set<Index>(const T& obj)
// в дополнении контейнер хранит массив из указателей на все серилизованные
данные в виде массива указателей на массивы байт каждого типа
serializedData[size()];
// сам контейнер является оберткой-Serializable над любым типом, что позволяет
составлять глубокое дерево из типов, с участием этого контейнера (см. ниже)

// class Data : public enable_serialization_from_this<std::vector<std::byte>>
{};
// template <typename I> class Index : public enable_serialization_from_this<I,
Data> {};

// если переводить на язык protobuf:
// message Data{
//   byte[] bytes;
// };
// message Index<I> {
//   I i = 1;
//   Data d = 2;
// };

// Пример использования: Index<T> index;
// index.serialize() -> std::same_as<const uuint8_t*>; // сериализует все
внутренние типы в массив байтов uuint8_t[SIZE]
// index.deserialize(const uuint8_t* data) -> std::same_as<bool>; // возвращает
true если удалось привести массив байт к нашему контейнеру по "типизированному
контракту"

// Пример использования: Index<T> index;
// memcpy(fd[0], index.serialize(), index.size()); // сериализовали данные
// index.deserialize(some_ptr_to_array); index.get<0>(); // десериализовали и
получили обьект по индексу 0
// index.getSerializedData(); // сразу получили указатель на серилизованные
данные, т.к. они хранятся внутри и меняются только при изменении значений

TODO: сделать поддержку std::string
TODO: реализовать рекурсивную сериализацию сразу в файл по заданным параметрам
TODO: реализовать сериализацию байт-код инструкций для работы контейнера
(
  что-то типо:
  перед тем как десерилизовать данные и записывать их в контейнер, проверь, что
поле age = get<0>() != 0;
  <- отдельная инструкция, записанная в начале блока данных
)

*/
template <typename T, Serializable... Args> class enable_serialization_from_this {
public:
  static const uint32_t type_count = 1 + sizeof...(Args);

private:
  std::tuple<T, Args...> data;
  std::array<uint32_t, type_count> sizes;
  uint8_t* byte_data;

protected:
  // TEMPLATE INITIALIZE ELEMENETS SIZES FOR SOME OPTIMIZATIONS
  template <uint32_t Index, typename CurrentType> static int32_t constexpr get_size() {
      if constexpr (std::is_trivially_copyable_v<CurrentType> && !serializable<CurrentType>) {
        return sizeof(CurrentType);
      } else if constexpr (serializable<CurrentType>) {
        return CurrentType::size();
      } else if constexpr (std::ranges::range<CurrentType>) {
        return std::tuple_size<CurrentType>::value;
      } else {
        throw std::logic_error("Size of type is not static");
      }
  }

  // RETURNS SUMMARY OF SIZE ELEMENETS
  template <uint32_t Index, uint64_t Size, typename First, typename... Rest> static uint64_t constexpr get_all_size() {
    uint64_t size = get_size<Index, First>();
      if constexpr (sizeof...(Rest) > 0) {
        return get_all_size<Index + 1, size + Size, Rest...>();
    } else
      return Size;
  }

  // TEMPLATE INITIALIZE ELEMENETS SIZES FOR SOME OPTIMIZATIONS
private:
  template <uint32_t Index, typename CurrentType> void initialize_size() { sizes[Index] = get_size<Index, CurrentType>(); }

  template <uint32_t Index = 0, typename First, typename... Rest> void initialize_sizes() {
    initialize_size<Index, First>();
      if constexpr (sizeof...(Rest) > 0) { initialize_sizes<Index + 1, Rest...>(); }
  }

  // INITIALIZE BYTES DATA FOR ALL ELEMENTS
private:
  template <uint32_t Index, uint64_t Split, typename CurrentType> void initialize_byte_data() {
      if constexpr (std::is_trivially_copyable_v<CurrentType> && !serializable<CurrentType>) {

        serialize(std::get<Index>(data), byte_data + Split);

      } else if constexpr (serializable<CurrentType>) {
        memcpy(byte_data + Split, std::get<Index>(data).serialize(), sizes[Index]);
      } else {
        throw std::logic_error("Type is not serializable");
      }
  }

  template <uint32_t Index = 0, uint32_t Split = 0, typename First, typename... Rest> void initialize_byte_datas() {
    initialize_byte_data<Index, Split, First>();

      if constexpr (sizeof...(Rest) > 0) { initialize_byte_datas<Index + 1, Split + get_size<Index, First>(), Rest...>(); }
  }

  // RETURNS SIZE OF ELEMENT IN INDEX
protected:
  template <uint32_t N> constexpr uint32_t get_size_elem() {
    static_assert(N < type_count, "Index out of range");
    return sizes[N];
  }

  // INTERFACE: SERIALIZABLE
  // RETURNS SUMMARY OF SIZE ELEMENETS
public:
  static uint64_t size() {
    uint64_t total = get_all_size<0, 0, T, Args...>();
    return total;
  }

  // RETURNS POINTER TO START BINARY DATA FOR SOME ELEMENET
protected:
  template <uint32_t N> uint8_t* get_split_data() const { return get_split_data<N>(byte_data); }

  // INTERFACE: SERIALIZABLE
  // RETURNS POINTER TO BINARY DATA OF ALL THIS CONTAINER DATA
public:
  const uint8_t* serialize() const { return byte_data; }

private:
  template <uint32_t I, uint32_t N> constexpr uint32_t compute_split() {
      if constexpr (I < N) {
        return get_size_elem<I>() + compute_split<I + 1, N>();
      } else {
        return 0;
      }
  }

  template <uint32_t N> const uint8_t* get_split(const uint8_t* data) {
    uint32_t split = compute_split<0, N>();
    return data + split;
  }

  // DESERIALIZE DATA IN CONTAINER FROM POINTER TO BINARY DATA
protected:
  template <uint32_t Index, typename CurrentType> bool deserialize(const uint8_t* data) {
      if constexpr (std::is_trivially_copyable_v<CurrentType> || serializable<CurrentType>) {
        auto p = get<Index>();

        bool result = deserialize(p, data);

        std::get<Index>(this->data) = p;
        return result;
      } else {
        throw std::logic_error("Type is not serializable");
      }
  }

  template <uint32_t Index = 0, typename First, typename... Rest> bool deserialize_all(const uint8_t* data) {
    if (!deserialize<Index, First>(get_split<Index>(data))) return false;
      if constexpr (sizeof...(Rest) > 0) {
        return deserialize_all<Index + 1, Rest...>(data);
      } else {
        return true;
      }
  }

  // INTERFACE: SERIALIZABLE
public:
  bool deserialize(const uint8_t* data) { return deserialize_all<0, T, Args...>(data); }

  // CTOR
public:
  enable_serialization_from_this() : byte_data(new uint8_t[type_count]) {
    initialize_sizes<0, T, Args...>();
    initialize_byte_datas<0, 0, T, Args...>();
  };

  // CTOR
protected:
  explicit enable_serialization_from_this(T&& t, Args&&... args) : data(std::forward<T>(t), std::forward<Args>(args)...), byte_data(new uint8_t[type_count]) {
    initialize_sizes<0, T, Args...>();
    initialize_byte_datas<0, 0, T, Args...>();
  }

  // GETTERS AND SETTERS
public:
  template <uint32_t Index> decltype(auto) get() const {
    static_assert(Index < sizeof...(Args) + 1, "Index out of bounds");
    return std::get<Index>(data);
  }

public:
  template <uint32_t Index, typename U> void set(const U& value) {
    static_assert(Index < sizeof...(Args) + 1, "Index out of bounds");
    std::get<Index>(data) = value;
  }

public:
  template <uint32_t Index, typename U> void set_and_serialize(const U& value) {
    static_assert(Index < sizeof...(Args) + 1, "Index out of bounds");
    std::get<Index>(data) = value;
    serialize(std::get<Index>(data), get_split_data<Index>());
  }

public:
  void serialize_all() {
    initialize_sizes<0, T, Args...>();
    initialize_byte_datas<0, 0, T, Args...>();
  }

  // FOR_EACH
public:
  template <typename Func> void for_each(Func&& func) {
    std::apply([&func](auto&&... elements) { (func(std::forward<decltype(elements)>(elements)), ...); }, data);
  }

  // OPERATORS
public:
  bool operator>(const enable_serialization_from_this& other) const { return data > other.data; }
  bool operator<(const enable_serialization_from_this& other) const { return data < other.data; }

  bool operator<=(const enable_serialization_from_this& other) const { return data <= other.data; }
  bool operator>=(const enable_serialization_from_this& other) const { return data >= other.data; }

  bool operator==(const enable_serialization_from_this& other) const { return data == other.data; }
  bool operator!=(const enable_serialization_from_this& other) const { return data != other.data; }

  // SERIALIZE OBJECTS
protected:
  template <typename U> void serialize(const U& obj, uint8_t* data) {
      if constexpr (std::is_trivially_copyable_v<U>) {
        memcpy(data, &obj, sizeof(U));
      } else {
        throw std::logic_error("Type is not trivially copyable");
      }
    std::cout << "Serialize trivially copyable type" << std::endl;
  }

  // SERIALIZE OBJECTS
protected:
  template <serializable U> void serialize(const U& obj, uint8_t* data) {
    std::cout << "Serialize serializable object" << std::endl;
    memcpy(data, obj.serialize(), obj.size());
  }

  // DESERIALIZE OBJECTS
protected:
  template <typename U> bool deserialize(U& obj, const uint8_t* data) {
      if constexpr (std::is_trivially_copyable_v<U>) {
        memcpy(&obj, data, sizeof(U));
        return true;
      } else {
        throw std::logic_error("Type is not trivially copyable");
      }
    std::cout << "Deserialize trivially copyable type" << std::endl;
  }

  // DESERIALIZE OBJECTS
protected:
  template <serializable U> bool deserialize(U& obj, const uint8_t* data) {
    std::cout << "Deserialize serializable object" << std::endl;
    return obj.deserialize(data);
  }

  // SOME SPECIAL TYPES THAT I WANT TO DEFINE
protected:
  void serialize(const std::string& obj, uint8_t* data) {
    std::cout << "Serialize std::string: " << obj << std::endl;
    uint32_t size = obj.size();
    std::memcpy(data, &size, sizeof(size));
    data += sizeof(size);
    std::memcpy(data, obj.data(), size);
  }

  void serialize(const uint64_t& obj, uint8_t* data) {
    std::cout << "Serialize uint64_t: " << obj << std::endl;
    std::memcpy(data, &obj, sizeof(obj));
  }

  void serialize(const uint32_t& obj, uint8_t* data) {
    std::cout << "Serialize uint32_t: " << obj << std::endl;
    std::memcpy(data, &obj, sizeof(obj));
  }

  void serialize(const uint8_t& obj, uint8_t* data) {
    std::cout << "Serialize uint8_t: " << static_cast<int>(obj) << std::endl;
    std::memcpy(data, &obj, sizeof(obj));
  }

  void serialize(const char& obj, uint8_t* data) {
    std::cout << "Serialize char: " << obj << std::endl;
    std::memcpy(data, &obj, sizeof(obj));
  }

  void serialize(const bool& obj, uint8_t* data) {
    std::cout << "Serialize bool: " << std::boolalpha << obj << std::endl;
    std::memcpy(data, &obj, sizeof(obj));
  }

  // SOME SPECIAL TYPES THAT I WANT TO DEFINE
protected:
  bool deserialize(std::string& obj, const uint8_t* data) {
    std::cout << "Deserialize std::string" << std::endl;
    uint32_t size = 0;
    std::memcpy(&size, data, sizeof(size));
    data += sizeof(size);
    obj.assign(reinterpret_cast<const char*>(data), size);
    std::cout << obj << std::endl;
    return true;
  }

  bool deserialize(uint64_t& obj, const uint8_t* data) {
    std::cout << "Deserialize uint64_t" << std::endl;
    std::memcpy(&obj, data, sizeof(obj));
    std::cout << obj << std::endl;
    return true;
  }

  bool deserialize(uint32_t& obj, const uint8_t* data) {
    std::cout << "Deserialize uint32_t" << std::endl;
    std::memcpy(&obj, data, sizeof(obj));
    std::cout << obj << std::endl;
    return true;
  }

  bool deserialize(uint8_t& obj, const uint8_t* data) {
    std::cout << "Deserialize uint8_t" << std::endl;
    std::memcpy(&obj, data, sizeof(obj));
    std::cout << obj << std::endl;
    return true;
  }

  bool deserialize(char& obj, const uint8_t* data) {
    std::cout << "Deserialize char" << std::endl;
    std::memcpy(&obj, data, sizeof(obj));
    std::cout << obj << std::endl;
    return true;
  }

  bool deserialize(bool& obj, const uint8_t* data) {
    std::cout << "Deserialize bool" << std::endl;
    std::memcpy(&obj, data, sizeof(obj));
    std::cout << obj << std::endl;
    return true;
  }

  // SERIALIZE FOR CONTAINERS
protected:
  template <typename Container>
    requires std::ranges::range<Container>
  void serialize(const Container& obj, uint8_t* data) {
    using ElementType = typename Container::value_type;

    serialize(std::ranges::size(obj), data);
    data += sizeof(uint32_t);
    std::cout << "Serialized container!" << std::endl;
      for (const auto& element : obj) {
        serialize(element, data);
          if constexpr (std::is_trivially_copyable_v<ElementType> && !serializable<ElementType>) {
            data += sizeof(ElementType);
          } else if (Serializable<ElementType>) {
            data += element.size();
          } else {
            throw std::logic_error("Type in container is not serializable");
          }
      }
  }

  // DESERIALIZE FOR CONTAINERS
protected:
  template <typename Container>
    requires std::ranges::range<Container>
  bool deserialize(Container& obj, const uint8_t* data) {
    using ElementType = typename Container::value_type;

    std::cout << "Deserialize container\n";
    uint64_t size;
    if (!deserialize(size, data)) return false;

    data += sizeof(uint32_t);
    // obj.resize(size); // disabled for std::array

      for (auto& element : obj) {
        if (!deserialize(element, data)) return false;

          if constexpr (std::is_trivially_copyable_v<ElementType> && !serializable<ElementType>) {
            data += sizeof(ElementType);
          } else if (serializable<ElementType>) {
            data += element.size();
          } else {
            throw std::logic_error("Type in container is not serializable");
          }
      }
    return true;
  }

public:
  // TODO: поддержка tuple
  // template <typename Tuple, uint32_t... Indices> static auto
  // tuple_to_enable_serialize_impl(Tuple&& tuple,
  // std::index_sequence<Indices...>) {
  //   return tuple_to_enable_serialize_impl<std::tuple_element_t<Indices,
  //   Tuple>...>(std::get<Indices>(std::forward<Tuple>(tuple))...);
  // }
  //
  // template <typename Tuple> static auto tuple_to_enable_serialize(Tuple&&
  // tuple) {
  //   return tuple_to_enable_serialize_impl(std::forward<Tuple>(tuple),
  //   std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
  // }

public:
  ~enable_serialization_from_this() { delete[] byte_data; }
};



}  // namespace Core
