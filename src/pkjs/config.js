// Settings-page text based on the phone language
// (navigator.language). English by default.
var language = (navigator.language || 'en').substring(0, 2);

var TEXTS = {
  en: {
    intro: 'This information is used to compute heart-rate zones, calories and distance. It stays on your phone and your watch.',
    profile: 'Profile', sex: 'Sex', male: 'Male', female: 'Female',
    age: 'Age (years)', height: 'Height (cm)', weight: 'Weight (kg)', stride: 'Running stride (cm)', walkStride: 'Walking stride (cm)',
    save: 'Save', units: 'Distance units', imperial: 'Imperial (mi)', metric: 'Metric (km)'
  },
  fr: {
    intro: 'Ces informations servent \u00e0 calculer les zones cardiaques, les calories et la distance parcourue. Elles restent sur votre t\u00e9l\u00e9phone et votre montre.',
    profile: 'Profil', sex: 'Sexe', male: 'Homme', female: 'Femme',
    age: '\u00c2ge (ans)', height: 'Taille (cm)', weight: 'Poids (kg)', stride: 'Foul\u00e9e de course (cm)',
    save: 'Enregistrer', units: 'Unités de distance', imperial: 'Impérial (mi)', metric: 'Métrique (km)'
  },
  de: {
    intro: 'Diese Angaben dienen zur Berechnung der Herzfrequenzzonen, Kalorien und Strecke. Sie bleiben auf dem Telefon und auf der Uhr.',
    profile: 'Profil', sex: 'Geschlecht', male: 'M\u00e4nnlich', female: 'Weiblich',
    age: 'Alter (Jahre)', height: 'Gr\u00f6\u00dfe (cm)', weight: 'Gewicht (kg)', stride: 'Laufschrittl\u00e4nge (cm)', walkStride: 'Gehschrittl\u00e4nge (cm)',
    save: 'Speichern', units: 'Entfernungseinheiten', imperial: 'Imperial (mi)', metric: 'Metrisch (km)'
  },
  es: {
    intro: 'Estos datos sirven para calcular las zonas de frecuencia card\u00edaca, las calor\u00edas y la distancia recorrida. Se quedan en el tel\u00e9fono y en el reloj.',
    profile: 'Perfil', sex: 'Sexo', male: 'Hombre', female: 'Mujer',
    age: 'Edad (a\u00f1os)', height: 'Altura (cm)', weight: 'Peso (kg)', stride: 'Zancada al correr (cm)', walkStride: 'Zancada al caminar (cm)',
    save: 'Guardar', units: 'Unidades de distancia', imperial: 'Imperial (mi)', metric: 'Métrico (km)'
  },
  it: {
    intro: 'Questi dati servono a calcolare le zone cardiache, le calorie e la distanza percorsa. Restano sul telefono e sull\'orologio.',
    profile: 'Profilo', sex: 'Sesso', male: 'Uomo', female: 'Donna',
    age: 'Et\u00e0 (anni)', height: 'Altezza (cm)', weight: 'Peso (kg)', stride: 'Lunghezza falcata (cm)', walkStride: 'Passo camminando (cm)',
    save: 'Salva', units: 'Unità di distanza', imperial: 'Imperiale (mi)', metric: 'Metrico (km)'
  },
  pt: {
    intro: 'Estes dados servem para calcular as zonas de frequ\u00eancia card\u00edaca, as calorias e a dist\u00e2ncia percorrida. Ficam no telefone e no rel\u00f3gio.',
    profile: 'Perfil', sex: 'Sexo', male: 'Masculino', female: 'Feminino',
    age: 'Idade (anos)', height: 'Altura (cm)', weight: 'Peso (kg)', stride: 'Passada de corrida (cm)', walkStride: 'Passada ao caminhar (cm)',
    save: 'Guardar', units: 'Unidades de distância', imperial: 'Imperial (mi)', metric: 'Métrico (km)'
  }
};

var t = TEXTS[language] || TEXTS.en;

module.exports = [
  { "type": "heading", "defaultValue": "RunZone" },
  { "type": "text", "defaultValue": t.intro },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": t.profile },
      {
        "type": "radiogroup",
        "messageKey": "Sex",
        "label": t.sex,
        "defaultValue": "1",
        "options": [
          { "label": t.male, "value": "1" },
          { "label": t.female, "value": "0" }
        ]
      },
      { "type": "slider", "messageKey": "Age", "label": t.age,
        "defaultValue": 35, "min": 10, "max": 100, "step": 1 },
      { "type": "slider", "messageKey": "Height", "label": t.height,
        "defaultValue": 175, "min": 120, "max": 230, "step": 1 },
      { "type": "slider", "messageKey": "Weight", "label": t.weight,
        "defaultValue": 75, "min": 30, "max": 250, "step": 1 },
      { "type": "slider", "messageKey": "Stride", "label": t.stride,
        "defaultValue": 79, "min": 40, "max": 200, "step": 1 },
      { "type": "slider", "messageKey": "WalkStride", "label": t.walkStride,
        "defaultValue": 75, "min": 40, "max": 200, "step": 1 },
      {
        "type": "radiogroup",
        "messageKey": "UnitSystem",
        "label": t.units,
        "defaultValue": "0",
        "options": [
          { "label": t.imperial, "value": "0" },
          { "label": t.metric, "value": "1" }
        ]
      }
    ]
  },
  { "type": "submit", "defaultValue": t.save }
];
