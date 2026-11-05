const labels = {
  not_provided: 'Источник не предоставил обложку', waiting: 'Загрузка обложки', sent: 'PNG отправлена; подтверждения компонента пока нет', applied: 'Компонент передал обложку Windows', native_error: 'Компонент не смог передать обложку Windows', conversion_error: 'Не удалось преобразовать обложку в PNG',
  network_error: 'Не удалось загрузить обложку', invalid_image_type: 'Сервер вернул другой тип файла', too_large: 'Обложка больше 256 КиБ', cancelled_or_timeout: 'Загрузка отменена или истекло время'
};
async function refresh() {
  try {
    const state = await chrome.runtime.sendMessage({type: 'connection_status'});
    document.getElementById('connection').textContent = state.connected ? 'Подключено к Stillbar' : 'Связи с приложением нет';
    document.getElementById('sources').textContent = state.sources.length ? state.sources.map((source, index) => `Источник ${index + 1}: ${source.playing ? 'играет' : 'пауза'}\n${labels[source.artwork] || source.artwork}${source.artworkBytes ? ` (${source.artworkBytes} байт)` : ''}${source.artworkError ? `: ${source.artworkError}` : ''}`).join('\n\n') : 'Медиаисточников нет. Обновите вкладку и включите воспроизведение.';
  } catch { document.getElementById('connection').textContent = 'Не удалось проверить связь'; }
}
document.getElementById('refresh').addEventListener('click', refresh);
document.getElementById('reload-tab').addEventListener('click', () => chrome.tabs.reload());
document.getElementById('reload-extension')?.addEventListener('click', () => chrome.runtime.reload());
document.getElementById('open-extensions').addEventListener('click', async () => {
  const result = await chrome.runtime.sendMessage({type: 'open_extensions'});
  if (!result?.opened) document.getElementById('connection').textContent = 'Откройте browser://extensions вручную через адресную строку';
});
refresh();
