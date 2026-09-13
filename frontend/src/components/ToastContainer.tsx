import { useRiggingStore } from '@/stores/useRiggingStore'

export interface Toast {
  id: string
  type: 'success' | 'error' | 'warning' | 'info'
  title: string
  message: string
  duration: number
  timestamp: number
}

export const ToastContainer: React.FC = () => {
  const toasts = useRiggingStore((state) => state.toasts)
  const removeToast = useRiggingStore((state) => state.removeToast)
  
  if (toasts.length === 0) return null
  
  return (
    <div className="fixed top-4 right-4 z-50 space-y-2 max-w-sm">
      {toasts.map((toast) => (
        <div
          key={toast.id}
          className={`p-4 rounded-lg shadow-lg border backdrop-blur-sm transition-all ${
            toast.type === 'success'
              ? 'bg-green-900/90 border-green-500 text-green-100'
              : toast.type === 'error'
              ? 'bg-red-900/90 border-red-500 text-red-100'
              : toast.type === 'warning'
              ? 'bg-yellow-900/90 border-yellow-500 text-yellow-100'
              : 'bg-blue-900/90 border-blue-500 text-blue-100'
          }`}
        >
          <div className="flex items-center justify-between">
            <div>
              <h4 className="font-medium text-sm">{toast.title}</h4>
              <p className="text-xs mt-1 opacity-80">{toast.message}</p>
            </div>
            <button
              onClick={() => removeToast(toast.id)}
              className="ml-4 text-current opacity-60 hover:opacity-100"
            >
              ✕
            </button>
          </div>
        </div>
      ))}
    </div>
  )
}