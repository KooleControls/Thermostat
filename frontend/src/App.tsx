import { SidebarProvider, SidebarTrigger } from "@/components/ui/sidebar"
import { AppSidebar, type Page } from "@/components/AppSidebar"
import { useRoute } from "@/hooks/use-route"
import { useAuth } from "@/hooks/use-auth"
import ThermostatPage from "@/pages/ThermostatPage"
import DiagnosticsPage from "@/pages/DiagnosticsPage"
import ThermalPage from "@/pages/ThermalPage"
import ConsolePage from "@/pages/ConsolePage"
import SettingsPage from "@/pages/SettingsPage"
import FirmwarePage from "@/pages/FirmwarePage"
import InfoPage from "@/pages/InfoPage"
import LoginPage from "@/pages/LoginPage"

function PageContent({ page }: { page: Page }) {
  switch (page) {
    case "thermostat":
      return <ThermostatPage />
    case "diagnostics":
      return <DiagnosticsPage />
    case "thermal":
      return <ThermalPage />
    case "console":
      return <ConsolePage />
    case "settings":
      return <SettingsPage />
    case "firmware":
      return <FirmwarePage />
    case "info":
      return <InfoPage />
  }
}

export default function App() {
  const { authenticated, checking } = useAuth()
  const { page, navigate } = useRoute()

  if (checking) return null   // stored token being validated — avoid login-page flash
  if (!authenticated) return <LoginPage />

  return (
    <SidebarProvider>
      <AppSidebar currentPage={page} onNavigate={navigate} />
      <main className="flex h-screen w-full min-w-0 flex-col overflow-hidden p-6">
        <SidebarTrigger className="shrink-0 md:hidden" />
        <div className="min-h-0 w-full flex-1 overflow-y-auto">
          <PageContent page={page} />
        </div>
      </main>
    </SidebarProvider>
  )
}
