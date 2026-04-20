#ifndef CACHEDFONTMETRICS_H
#define CACHEDFONTMETRICS_H

#include "common/Metrics.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <QFont>
#include <QFontMetrics>
#include <QObject>

template<typename T>
class CachedFontMetrics
{
public:
    explicit CachedFontMetrics(const QFont &font)
        : mFontMetrics(font)
        , mAsciiWidths{}
    {
        mHeight = mFontMetrics.height();
    }

    static std::shared_ptr<CachedFontMetrics<T>> forFont(const QFont &font)
    {
        static std::unordered_map<std::string, std::weak_ptr<CachedFontMetrics<T>>> cache;
        const std::string key = font.key().toStdString();
        auto it = cache.find(key);
        if (it != cache.end()) {
            if (auto sp = it->second.lock()) {
                return sp;
            }
        }
        auto sp = std::make_shared<CachedFontMetrics<T>>(font);
        cache[key] = sp;
        return sp;
    }

    T width(const QChar &ch)
    {
        auto unicode = ch.unicode();
        if (unicode < kAsciiCacheSize) {
            if (!mAsciiWidths[unicode]) {
                mAsciiWidths[unicode] = fetchWidth(ch);
            }
            return mAsciiWidths[unicode];
        }
        if (unicode >= 0xD800 && unicode < 0xE000) {
            return fetchWidth(ch);
        }
        auto it = mWidths.find(unicode);
        if (it != mWidths.end()) {
            return it->second;
        }
        T w = fetchWidth(ch);
        mWidths.emplace(unicode, w);
        return w;
    }

    T width(const QString &text)
    {
        T result = 0;
        QChar temp;
        for (const QChar &ch : text) {
            if (ch.isHighSurrogate())
                temp = ch;
            else if (ch.isLowSurrogate())
                result += fetchWidth(QString(temp) + ch);
            else
                result += width(ch);
        }
        return result;
    }

    T height() { return mHeight; }

    T position(const QString &text, T offset)
    {
        T curWidth = 0;
        QChar temp;

        for (int i = 0; i < text.length(); i++) {
            QChar ch = text[i];

            if (ch.isHighSurrogate())
                temp = ch;
            else if (ch.isLowSurrogate())
                curWidth += fetchWidth(QString(temp) + ch);
            else
                curWidth += width(ch);

            if (curWidth >= offset) {
                return i;
            }
        }

        return -1;
    }

private:
    static constexpr int kAsciiCacheSize = 128;

    typename Metrics<T>::FontMetrics mFontMetrics;
    T mAsciiWidths[kAsciiCacheSize];
    std::unordered_map<uint16_t, T> mWidths;
    T mHeight;

    T fetchWidth(QChar c)
    {
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
        return mFontMetrics.width(c);
#else
        return mFontMetrics.horizontalAdvance(c);
#endif
    }

    T fetchWidth(const QString &s)
    {
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
        return mFontMetrics.width(s);
#else
        return mFontMetrics.horizontalAdvance(s);
#endif
    }
};

#endif // CACHEDFONTMETRICS_H
