"use client"

import { createContext, useContext, type ReactNode } from "react"

interface AuthContextValue {
  user: unknown | null
}

const AuthContext = createContext<AuthContextValue>({ user: null })

// Login state is implemented in issue FE-02.
export function AuthProvider({ children }: { children: ReactNode }) {
  return <AuthContext.Provider value={{ user: null }}>{children}</AuthContext.Provider>
}

export const useAuth = () => useContext(AuthContext)
