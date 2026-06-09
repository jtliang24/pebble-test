import Poco from "commodetto/Poco";

const render = new Poco(screen);

// Fonts
const timeFont = new render.Font("Bitham-Bold", 42);
const dateFont = new render.Font("Gothic-Bold", 24);

// Colors
const black = render.makeColor(0, 0, 0);
const white = render.makeColor(255, 255, 255);

const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];


function draw(event) {
    const now = event.date;

    render.begin();
    render.fillRectange(black, 0, 0, render.width, render.height);

    const hours = String(now.getHours()).padStart(2, "0");
    const minutes = String(now.getMinutes()).padStart(2, "0");
    const timeStr = `${hours}:${minutes}`;

    let width = render.getTextWidth(timeStr, timeFont);
    render.drawText(timeStr, timeFont, white,
      (render.width - width) / 2,
      (render.height / 2) - timeFont.height + 5);

    render.end();

}


watch.addEventListener("minutechange", draw);
