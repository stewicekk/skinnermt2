// Type declaration for CSS modules
/// <reference types="vite/client" />

declare module '*.css' {
  const content: string
  export default content
}