/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        dark: {
          bg: '#06091a',
          card: '#0b112c',
          cardHover: '#101736',
          border: '#17234d',
          borderHover: '#23366f',
          muted: '#8a9fc2',
          text: '#f1f5f9'
        },
        orbit: {
          blue: '#2563eb',
          blueHover: '#3b82f6',
          cyan: '#22d3ee',
          glow: '#1d4ed8'
        }
      }
    },
  },
  plugins: [],
}
