// Sprachumschaltung der Weboberfläche (Deutsch/Englisch).
// Stellt den Store `lang` (aktuelle Sprache, in localStorage gemerkt) und den abgeleiteten Store `t`
// (Übersetzungsfunktion) bereit. In Komponenten: $t("schluessel", { name: wert }); in .js-Dateien: get(t)(...).
import { writable, derived } from "svelte/store";
import de from "./i18n/de.js";
import en from "./i18n/en.js";

const dictionaries = { de, en };
const SUPPORTED = ["de", "en"];

// Liest die gespeicherte Sprache; ohne Speicher oder bei ungültigem Wert Deutsch
function loadLang() {
  try {
    const stored = localStorage.getItem("lang");
    if (SUPPORTED.includes(stored)) return stored;
  } catch {}
  return "de";
}

export const lang = writable(loadLang());

// Bei jeder Änderung: speichern und <html lang> nachziehen
lang.subscribe((value) => {
  try {
    localStorage.setItem("lang", value);
  } catch {}
  if (typeof document !== "undefined") document.documentElement.lang = value;
});

// Übersetzungsfunktion: Schlüssel in aktueller Sprache, sonst Deutsch, sonst der Schlüssel selbst;
// {platzhalter} werden aus params ersetzt
export const t = derived(lang, ($lang) => (key, params = {}) => {
  const text = dictionaries[$lang]?.[key] ?? de[key] ?? key;
  return text.replace(/\{(\w+)\}/g, (match, name) => (name in params ? String(params[name]) : match));
});

// Locale-Kennung für Datums-/Zeitformatierung (toLocaleString) in der aktuellen Sprache
export function localeFor(language) {
  return language === "en" ? "en-GB" : "de-DE";
}
