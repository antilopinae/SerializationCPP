# My serialization engine on templates with -O3:

Основная идея: сделать контейнер, который сам сериализует и десериализует любые тривиально-копируемые, стандартные типы или контейнеры из них,
А также типы, которые поддерживают интерфейс ```Serializable``` (реализуют методы `serialize()`, `size()`, `deserialize()`)

Контейнер также хранит внутри себя данные всех этих типов и предоставляет к ним доступ `T& get<Index>()`, `void set<Index>(const T& obj)`

В дополнении контейнер хранит массив из указателей на все серилизованные данные в виде массива указателей на массивы байт каждого типа `serializedData[size()]`;

Сам контейнер является оберткой-Serializable над любым типом, что позволяет составлять глубокое дерево из типов, с участием этого контейнера (см. ниже)

```
class Data : public enable_serialization_from_this<std::vector<std::byte>> {};
template <typename I> class Index : public enable_serialization_from_this<I, Data> {};
```
Если переводить на язык protobuf:
```
message Data{
  byte[] bytes;
};
message Index<I> {
  I i = 1;
  Data d = 2;
};
```

## Пример использования: Index<T> index;
```
index.serialize() -> std::same_as<const uuint8_t*>;
```
сериализует все внутренние типы в массив байтов uuint8_t[SIZE]
```
index.deserialize(const uuint8_t* data) -> std::same_as<bool>; 
```
возвращает true если удалось привести массив байт к нашему контейнеру по "типизированному контракту"

## Пример использования: Index<T> index;
```
memcpy(fd[0], index.serialize(), index.size()); сериализовали данные
index.deserialize(some_ptr_to_array); index.get<0>(); десериализовали и получили обьект по индексу 0
index.getSerializedData(); сразу получили указатель на серилизованные данные, т.к. они хранятся внутри и меняются только при изменении значений
```

TODO: сделать поддержку std::string
TODO: реализовать рекурсивную сериализацию сразу в файл по заданным параметрам
TODO: реализовать сериализацию байт-код инструкций для работы контейнера
```
(
  что-то типо:
  перед тем как десерилизовать данные и записывать их в контейнер, проверь, что
поле age = get<0>() != 0;
  <- отдельная инструкция, записанная в начале блока данных
)
```

# Вывод программы:
```
Serialize uint64_t: 4
Serialized container!
Serialize bool: false
Serialize bool: false
Serialize bool: false
Serialize bool: false

Deserialize container
Deserialize bool
true
Deserialize bool
false
Deserialize bool
true
Deserialize bool
false
```
