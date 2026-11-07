# Gets real forecast numbers from Open-Meteo, a free weather service that needs
# no account or key. This file only collects the data as text. Claude reads that
# text and writes the answer, so it can answer exactly what was asked.
import json                # turns the service's reply text into Python data
import urllib.parse        # makes text safe to put inside a web address
import urllib.request      # downloads a web address

# How long to wait for the weather service before giving up, in seconds.
TIMEOUT_SECONDS = 10

# Open-Meteo describes the sky with a number. These are the names for them.
WEATHER_NAMES = {
    0: "clear",
    1: "mostly clear",
    2: "partly cloudy",
    3: "cloudy",
    45: "fog",
    48: "fog",
    51: "light drizzle",
    53: "drizzle",
    55: "heavy drizzle",
    56: "freezing drizzle",
    57: "freezing drizzle",
    61: "light rain",
    63: "rain",
    65: "heavy rain",
    66: "freezing rain",
    67: "freezing rain",
    71: "light snow",
    73: "snow",
    75: "heavy snow",
    77: "snow grains",
    80: "light showers",
    81: "showers",
    82: "heavy showers",
    85: "snow showers",
    86: "snow showers",
    95: "thunderstorm",
    96: "thunderstorm with hail",
    99: "thunderstorm with hail",
}


def download_json(address):
    """Download a web address and return its JSON reply as Python data."""
    with urllib.request.urlopen(address, timeout=TIMEOUT_SECONDS) as reply:
        return json.load(reply)


def sky_name(code):
    """Turn Open-Meteo's sky number into words."""
    return WEATHER_NAMES.get(code, "mixed")


def get_weather_text(city):
    """Return the forecast for a city as plain text for Claude to read."""

    # Step 1: turn the city's name into a map position.
    # quote() makes the name safe for a web address, like a space becoming %20.
    address = (
        "https://geocoding-api.open-meteo.com/v1/search?count=1&name="
        + urllib.parse.quote(city)
    )
    places = download_json(address)

    # The "results" list is missing when nothing matched.
    if "results" not in places:
        return "No city found with the name: " + city

    place = places["results"][0]

    # Step 2: ask for the forecast at that position, in the city's own time zone.
    address = (
        "https://api.open-meteo.com/v1/forecast"
        + "?latitude=" + str(place["latitude"])
        + "&longitude=" + str(place["longitude"])
        + "&timezone=auto&forecast_days=7"
        + "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
        + "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
        + "precipitation_probability_max,precipitation_sum,wind_speed_10m_max,sunrise,sunset"
        + "&hourly=temperature_2m,precipitation_probability,weather_code"
    )
    forecast = download_json(address)

    # Step 3: write it all out as simple lines of text.
    lines = []
    lines.append("Place: " + place["name"] + ", " + place.get("admin1", "") + ", " + place.get("country", ""))

    now = forecast["current"]
    lines.append(
        "Now (" + now["time"] + "): " + sky_name(now["weather_code"])
        + ", " + str(now["temperature_2m"]) + "C"
        + ", feels like " + str(now["apparent_temperature"]) + "C"
        + ", wind " + str(now["wind_speed_10m"]) + " km/h"
    )

    # One line per day, for 7 days.
    lines.append("Daily:")
    daily = forecast["daily"]
    for day in range(len(daily["time"])):
        lines.append(
            daily["time"][day] + ": " + sky_name(daily["weather_code"][day])
            + ", low " + str(daily["temperature_2m_min"][day]) + "C"
            + ", high " + str(daily["temperature_2m_max"][day]) + "C"
            + ", rain chance " + str(daily["precipitation_probability_max"][day]) + "%"
            + ", rain " + str(daily["precipitation_sum"][day]) + " mm"
            + ", wind up to " + str(daily["wind_speed_10m_max"][day]) + " km/h"
            + ", sunrise " + daily["sunrise"][day][-5:]
            + ", sunset " + daily["sunset"][day][-5:]
        )

    # One line every 3 hours for the first 3 days, for questions like "this evening".
    lines.append("Every 3 hours:")
    hourly = forecast["hourly"]
    for hour in range(0, 72, 3):
        lines.append(
            hourly["time"][hour] + ": " + sky_name(hourly["weather_code"][hour])
            + ", " + str(hourly["temperature_2m"][hour]) + "C"
            + ", rain chance " + str(hourly["precipitation_probability"][hour]) + "%"
        )

    # Join the list into one piece of text, with a new line between each.
    return "\n".join(lines)


