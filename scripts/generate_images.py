from PIL import Image, ImageDraw, ImageFont
import os

# ==========================================
# 1. הגדרות בסיסיות 
# ==========================================
FONT_PATH = "NotoSansHebrew-Bold.ttf" # ודא שזה שם הפונט המדויק שלך
OUTPUT_FOLDER = r"Hebrew_Word_Clock/data"


# המילון המדויק והסופי (כולל תיקוני סמיכות וניקוד ו' החיבור)
words_dict = {
    "1": "אַחַת", "2": "שְׁתַּיִם", "3": "שָׁלוֹשׁ", "4": "אַרְבַּע", 
    "5": "חָמֵשׁ", "6": "שֵׁשׁ", "7": "שֶׁבַע", "8": "שְׁמוֹנֶה", 
    "9": "תֵּשַׁע", "10": "עֶשֶׂר",
    "20": "עֶשְׂרִים", "30": "שְׁלוֹשִׁים", "40": "אַרְבָּעִים", "50": "חֲמִשִּׁים",
    "11": "אַחַת עֶשְׂרֵה", "12": "שְׁתֵּים עֶשְׂרֵה", "13": "שְׁלוֹשׁ עֶשְׂרֵה", 
    "14": "אַרְבַּע עֶשְׂרֵה", "15": "חֲמֵשׁ עֶשְׂרֵה", "16": "שֵׁשׁ עֶשְׂרֵה", 
    "17": "שְׁבַע עֶשְׂרֵה", "18": "שְׁמוֹנֶה עֶשְׂרֵה", "19": "תְּשַׁע עֶשְׂרֵה",
    
    "and_1": "וְאַחַת", "and_2": "וּשְׁתֵּי", "and_3": "וְשָׁלוֹשׁ", "and_4": "וְאַרְבַּע", 
    "and_5": "וְחָמֵשׁ", "and_6": "וְשֵׁשׁ", "and_7": "וְשֶׁבַע", "and_8": "וּשְׁמוֹנֶה", 
    "and_9": "וְתֵשַׁע", 
    "and_10": "וְעֶשֶׂר", "and_20": "וְעֶשְׂרִים", "and_30": "וּשְׁלוֹשִׁים", 
    "and_40": "וְאַרְבָּעִים", "and_50": "וַחֲמִשִּׁים",
    
    "and_11": "וְאַחַת עֶשְׂרֵה", "and_12": "וּשְׁתֵּים עֶשְׂרֵה", 
    "and_13": "וּשְׁלוֹשׁ עֶשְׂרֵה", "and_14": "וְאַרְבַּע עֶשְׂרֵה", "and_15": "וַחֲמֵשׁ עֶשְׂרֵה", 
    "and_16": "וְשֵׁשׁ עֶשְׂרֵה", "and_17": "וּשְׁבַע עֶשְׂרֵה", "and_18": "וּשְׁמוֹנֶה עֶשְׂרֵה", 
    "and_19": "וּתְשַׁע עֶשְׂרֵה",
    
    "and_1_minute": "וְדַקָּה",
    "and_2_minutes": "וּשְׁתֵּי דַּקּוֹת",
    "quarter": "וָרֶבַע", 
    "half": "וָחֵצִי", 
    
    "exactly": "בְּדִיּוּק",
    "minute": "דַּקָּה", 
    "minutes": "דַּקּוֹת",
    "morning": "בַּבֹּקֶר", 
    "noon": "בַּצָּהֳרַיִם", 
    "afternoon": "אַחַר הַצָּהֳרַיִם", 
    "evening": "בָּעֶרֶב", 
    "night": "בַּלַּיְלָה",
    "early_morning": "לִפְנוֹת בֹּקֶר"
}

# ==========================================
# 3. תהליך יצירת התמונות (עם מנוע יישור מתקדם)
# ==========================================
FONT_SIZE = 65 

def generate_images():
    if not os.path.exists(OUTPUT_FOLDER):
        os.makedirs(OUTPUT_FOLDER)

    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    dummy_img = Image.new('L', (1, 1), color=255)
    draw_dummy = ImageDraw.Draw(dummy_img)

    # 1. חישוב תיבת התיחום הגלובלית (Global Bounding Box)
    # אנחנו בודקים את כל המילים כדי למצוא את הפיקסל הגבוה ביותר והנמוך ביותר
    min_y = 0
    max_y = 0
    
    for word in words_dict.values():
        bbox = draw_dummy.textbbox((0, 0), word, font=font, direction='rtl')
        if bbox[1] < min_y: min_y = bbox[1]
        if bbox[3] > max_y: max_y = bbox[3]

    padding = 10
    # הגובה הקבוע לכל התמונות ייקבע לפי המילה הכי גבוהה + הכי נמוכה
    fixed_height = (max_y - min_y) + (padding * 2) 
    # היסט קבוע על ציר Y שיבטיח ששורת הבסיס (Baseline) זהה לכולן
    fixed_y_offset = padding - min_y

    # 2. ציור התמונות
    for filename, word in words_dict.items():
        # לכל מילה יש רוחב משלה
        bbox = draw_dummy.textbbox((0, 0), word, font=font, direction='rtl')
        width = bbox[2] - bbox[0] + (padding * 2)
        
        # התמונה תיווצר עם רוחב משתנה אבל גובה אחיד לגמרי
        image = Image.new('L', (width, fixed_height), color=255)
        draw = ImageDraw.Draw(image)

        # ציור הטקסט עם היסט X דינמי והיסט Y גלובלי
        x_offset = padding - bbox[0]
        draw.text((x_offset, fixed_y_offset), word, font=font, fill=0, direction='rtl')

        # המרה ל-1-bit (שחור/לבן מוחלט עבור e-Ink)
        image = image.point(lambda x: 0 if x < 128 else 255, '1')

        save_path = os.path.join(OUTPUT_FOLDER, f"{filename}.bmp")
        image.save(save_path)
        print(f"Generated: {filename}.bmp -> {word}")

    print(f"\nSuccess! Generated {len(words_dict)} perfectly aligned images in '{OUTPUT_FOLDER}'.")

if __name__ == "__main__":
    generate_images()