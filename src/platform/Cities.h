#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// The places prayer times can be calculated for without asking anyone: a bundled list, no network. Good to about a
// kilometre, which is a second of prayer time. "Custom" in Settings takes any latitude and longitude instead.
namespace Cities {

struct City { const char *name, *country; double lat, lon; };

inline const City *all(int *count)
{
    static const City list[] = {
        // Indonesia
        {"Jakarta", "Indonesia", -6.2088, 106.8456}, {"Bandung", "Indonesia", -6.9175, 107.6191}, {"Surabaya", "Indonesia", -7.2575, 112.7521},
        {"Semarang", "Indonesia", -6.9667, 110.4167}, {"Yogyakarta", "Indonesia", -7.7956, 110.3695}, {"Surakarta (Solo)", "Indonesia", -7.5666, 110.8283},
        {"Malang", "Indonesia", -7.9839, 112.6214}, {"Bogor", "Indonesia", -6.5950, 106.8166}, {"Depok", "Indonesia", -6.4025, 106.7942},
        {"Bekasi", "Indonesia", -6.2383, 106.9756}, {"Tangerang", "Indonesia", -6.1783, 106.6319}, {"Serang", "Indonesia", -6.1201, 106.1503},
        {"Cirebon", "Indonesia", -6.7063, 108.5570}, {"Tasikmalaya", "Indonesia", -7.3274, 108.2207}, {"Medan", "Indonesia", 3.5952, 98.6722},
        {"Banda Aceh", "Indonesia", 5.5483, 95.3238}, {"Padang", "Indonesia", -0.9471, 100.4172}, {"Pekanbaru", "Indonesia", 0.5071, 101.4478},
        {"Batam", "Indonesia", 1.0456, 104.0305}, {"Tanjung Pinang", "Indonesia", 0.9186, 104.4558}, {"Jambi", "Indonesia", -1.6101, 103.6131},
        {"Palembang", "Indonesia", -2.9761, 104.7754}, {"Bengkulu", "Indonesia", -3.8004, 102.2655}, {"Bandar Lampung", "Indonesia", -5.4292, 105.2610},
        {"Pangkal Pinang", "Indonesia", -2.1316, 106.1169}, {"Pontianak", "Indonesia", -0.0263, 109.3425}, {"Palangkaraya", "Indonesia", -2.2161, 113.9135},
        {"Banjarmasin", "Indonesia", -3.3194, 114.5908}, {"Samarinda", "Indonesia", -0.5022, 117.1536}, {"Balikpapan", "Indonesia", -1.2654, 116.8312},
        {"Tarakan", "Indonesia", 3.3000, 117.6333}, {"Denpasar", "Indonesia", -8.6705, 115.2126}, {"Mataram", "Indonesia", -8.5833, 116.1167},
        {"Kupang", "Indonesia", -10.1772, 123.6070}, {"Makassar", "Indonesia", -5.1477, 119.4327}, {"Palu", "Indonesia", -0.8917, 119.8707},
        {"Kendari", "Indonesia", -3.9985, 122.5129}, {"Gorontalo", "Indonesia", 0.5435, 123.0568}, {"Manado", "Indonesia", 1.4748, 124.8421},
        {"Mamuju", "Indonesia", -2.6741, 118.8888}, {"Ambon", "Indonesia", -3.6954, 128.1814}, {"Ternate", "Indonesia", 0.7833, 127.3667},
        {"Jayapura", "Indonesia", -2.5337, 140.7181}, {"Manokwari", "Indonesia", -0.8615, 134.0620}, {"Sorong", "Indonesia", -0.8762, 131.2558},
        {"Merauke", "Indonesia", -8.4932, 140.4018},
        // Southeast Asia
        {"Kuala Lumpur", "Malaysia", 3.1390, 101.6869}, {"Johor Bahru", "Malaysia", 1.4927, 103.7414}, {"George Town (Penang)", "Malaysia", 5.4141, 100.3288},
        {"Kota Kinabalu", "Malaysia", 5.9804, 116.0735}, {"Kuching", "Malaysia", 1.5535, 110.3593}, {"Singapore", "Singapore", 1.3521, 103.8198},
        {"Bandar Seri Begawan", "Brunei", 4.9031, 114.9398}, {"Bangkok", "Thailand", 13.7563, 100.5018}, {"Manila", "Philippines", 14.5995, 120.9842},
        // Middle East and Africa
        {"Makkah", "Saudi Arabia", 21.4225, 39.8262}, {"Madinah", "Saudi Arabia", 24.4672, 39.6111}, {"Riyadh", "Saudi Arabia", 24.7136, 46.6753},
        {"Jeddah", "Saudi Arabia", 21.4858, 39.1925}, {"Dubai", "UAE", 25.2048, 55.2708}, {"Abu Dhabi", "UAE", 24.4539, 54.3773},
        {"Doha", "Qatar", 25.2854, 51.5310}, {"Kuwait City", "Kuwait", 29.3759, 47.9774}, {"Manama", "Bahrain", 26.2285, 50.5860},
        {"Muscat", "Oman", 23.5880, 58.3829}, {"Amman", "Jordan", 31.9454, 35.9284}, {"Jerusalem", "Palestine", 31.7683, 35.2137},
        {"Baghdad", "Iraq", 33.3152, 44.3661}, {"Tehran", "Iran", 35.6892, 51.3890}, {"Istanbul", "Turkey", 41.0082, 28.9784},
        {"Ankara", "Turkey", 39.9334, 32.8597}, {"Cairo", "Egypt", 30.0444, 31.2357}, {"Alexandria", "Egypt", 31.2001, 29.9187},
        {"Casablanca", "Morocco", 33.5731, -7.5898}, {"Algiers", "Algeria", 36.7538, 3.0588}, {"Tunis", "Tunisia", 36.8065, 10.1815},
        {"Khartoum", "Sudan", 15.5007, 32.5599}, {"Lagos", "Nigeria", 6.5244, 3.3792}, {"Nairobi", "Kenya", -1.2921, 36.8219},
        {"Dar es Salaam", "Tanzania", -6.7924, 39.2083},
        // South and Central Asia
        {"Karachi", "Pakistan", 24.8607, 67.0011}, {"Lahore", "Pakistan", 31.5204, 74.3587}, {"Islamabad", "Pakistan", 33.6844, 73.0479},
        {"Dhaka", "Bangladesh", 23.8103, 90.4125}, {"Delhi", "India", 28.6139, 77.2090}, {"Mumbai", "India", 19.0760, 72.8777},
        {"Kabul", "Afghanistan", 34.5553, 69.2075}, {"Tashkent", "Uzbekistan", 41.2995, 69.2401}, {"Baku", "Azerbaijan", 40.4093, 49.8671},
        // Europe, Americas, Oceania, East Asia
        {"London", "United Kingdom", 51.5074, -0.1278}, {"Paris", "France", 48.8566, 2.3522}, {"Berlin", "Germany", 52.5200, 13.4050},
        {"Amsterdam", "Netherlands", 52.3676, 4.9041}, {"Brussels", "Belgium", 50.8503, 4.3517}, {"Madrid", "Spain", 40.4168, -3.7038},
        {"Rome", "Italy", 41.9028, 12.4964}, {"Stockholm", "Sweden", 59.3293, 18.0686}, {"Moscow", "Russia", 55.7558, 37.6173},
        {"New York", "USA", 40.7128, -74.0060}, {"Washington, DC", "USA", 38.9072, -77.0369}, {"Chicago", "USA", 41.8781, -87.6298},
        {"Houston", "USA", 29.7604, -95.3698}, {"Los Angeles", "USA", 34.0522, -118.2437}, {"Toronto", "Canada", 43.6532, -79.3832},
        {"Sydney", "Australia", -33.8688, 151.2093}, {"Melbourne", "Australia", -37.8136, 144.9631}, {"Perth", "Australia", -31.9505, 115.8605},
        {"Auckland", "New Zealand", -36.8509, 174.7645}, {"Tokyo", "Japan", 35.6762, 139.6503}, {"Seoul", "South Korea", 37.5665, 126.9780},
    };
    *count = int(sizeof(list) / sizeof(list[0]));
    return list;
}

inline int indexOf(const QString &name)
{
    int n = 0;
    const City *c = all(&n);
    for (int i = 0; i < n; ++i)
        if (name == QLatin1String(c[i].name))
            return i;
    return -1;
}

// A city for a system time zone, when the zone says where the person most likely is; "" otherwise.
inline QString guess(const QString &timeZoneId)
{
    static const struct { const char *zone, *city; } map[] = {
        {"Asia/Jakarta", "Jakarta"}, {"Asia/Makassar", "Makassar"}, {"Asia/Jayapura", "Jayapura"}, {"Asia/Pontianak", "Pontianak"},
        {"Asia/Kuala_Lumpur", "Kuala Lumpur"}, {"Asia/Kuching", "Kuching"}, {"Asia/Singapore", "Singapore"}, {"Asia/Brunei", "Bandar Seri Begawan"},
        {"Asia/Riyadh", "Riyadh"}, {"Asia/Dubai", "Dubai"}, {"Asia/Qatar", "Doha"}, {"Asia/Kuwait", "Kuwait City"}, {"Asia/Muscat", "Muscat"},
        {"Asia/Baghdad", "Baghdad"}, {"Asia/Tehran", "Tehran"}, {"Europe/Istanbul", "Istanbul"}, {"Africa/Cairo", "Cairo"},
        {"Asia/Karachi", "Karachi"}, {"Asia/Dhaka", "Dhaka"}, {"Asia/Kolkata", "Delhi"}, {"Africa/Casablanca", "Casablanca"},
        {"Europe/London", "London"}, {"Europe/Paris", "Paris"}, {"Europe/Berlin", "Berlin"}, {"Europe/Amsterdam", "Amsterdam"},
        {"America/New_York", "New York"}, {"America/Toronto", "Toronto"}, {"America/Chicago", "Chicago"}, {"America/Los_Angeles", "Los Angeles"},
        {"Australia/Sydney", "Sydney"}, {"Australia/Melbourne", "Melbourne"}, {"Australia/Perth", "Perth"},
    };
    for (const auto &m : map)
        if (timeZoneId == QLatin1String(m.zone))
            return QString::fromLatin1(m.city);
    return {};
}

}
