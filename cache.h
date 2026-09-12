#pragma once

#include <string>
#include <unordered_map>
#include <list>
#include <windows.h>

class LRUCache
{
private:
    int capacity;

    // Most Recently Used -> Least Recently Used
    std::list<std::string> recentKeys;

    // key -> {response, position in list}
    std::unordered_map<
        std::string,
        std::pair<
            std::string,
            std::list<std::string>::iterator
        >
    > cache;

    HANDLE cacheMutex;

public:
    LRUCache(int capacity);

    bool get(
        const std::string& key,
        std::string& value
    );

    void put(
        const std::string& key,
        const std::string& value
    );
};