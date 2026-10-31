export default function PagePlaceholder({ title, issue }: { title: string; issue: string }) {
  return (
    <main className="min-h-screen flex flex-col items-center justify-center gap-2 p-4">
      <h1 className="text-2xl font-bold">{title}</h1>
      <p className="text-gray-500">Chưa hiện thực, xem issue {issue}</p>
    </main>
  )
}
