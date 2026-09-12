#include "cache.h"

using namespace std;

LRUCache::LRUCache(int capacity)
{
    this->capacity = capacity;

    cacheMutex = CreateMutex(NULL, FALSE, NULL);
}

bool LRUCache::get(
    const string& key,
    string& value
)
{
    WaitForSingleObject(cacheMutex, INFINITE);

    auto it = cache.find(key);

    // Cache MISS
    if (it == cache.end())
    {
        ReleaseMutex(cacheMutex);
        return false;
    }

    // Cache HIT
    value = it->second.first;

    // Old position remove karo
    recentKeys.erase(
        it->second.second
    );

    // Front par daalo = Most Recently Used
    recentKeys.push_front(key);

    // New position store karo
    it->second.second =
        recentKeys.begin();

    ReleaseMutex(cacheMutex);

    return true;
}

void LRUCache::put(
    const string& key,
    const string& value
)
{
    WaitForSingleObject(cacheMutex, INFINITE);

    // Key already present hai
    auto it = cache.find(key);

    if (it != cache.end())
    {
        it->second.first = value;

        recentKeys.erase(
            it->second.second
        );

        recentKeys.push_front(key);

        it->second.second =
            recentKeys.begin();

        ReleaseMutex(cacheMutex);
        return;
    }

    // Cache full hai
    if (cache.size() >= capacity)
    {
        // Back = Least Recently Used
        string leastRecentKey =
            recentKeys.back();

        recentKeys.pop_back();

        cache.erase(
            leastRecentKey
        );
    }

    // New item front par
    recentKeys.push_front(key);

    cache[key] = {
        value,
        recentKeys.begin()
    };

    ReleaseMutex(cacheMutex);
}