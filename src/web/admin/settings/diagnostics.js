
  function togglePasswordVisibility(inputId, buttonEl) {
    const input = document.getElementById(inputId);
    if (!input || !buttonEl) return;
    const showLabel = buttonEl.dataset.labelShow || 'Show';
    const hideLabel = buttonEl.dataset.labelHide || 'Hide';
    const isHidden = input.type === 'password';
    input.type = isHidden ? 'text' : 'password';
    buttonEl.textContent = isHidden ? hideLabel : showLabel;
  }

  function updateOtaFileName(inputEl) {
    const nameEl = document.getElementById('ota_file_name');
    if (!nameEl) return;
    const file = inputEl && inputEl.files && inputEl.files.length ? inputEl.files[0] : null;
    nameEl.textContent = file ? file.name : t('otaNoFileSelected');
  }

  async function createScreenshotAndDownload() {
    showNotification(t('screenshotCreating'));
    try {
      const res = await fetch('/api/screenshot', { method: 'POST' });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.success) {
        throw new Error(data.error || t('screenshotFailed'));
      }
      showNotification(t('screenshotSaved'));
      const link = document.createElement('a');
      link.href = '/api/screenshot/download?ts=' + Date.now();
      link.download = 'ui_screenshot.jpg';
      document.body.appendChild(link);
      link.click();
      link.remove();
    } catch (err) {
      showNotification(err?.message || t('screenshotFailed'), false);
    }
  }

  // Frame times of the Assist screen, measured on the panel (assist_timing.h):
  // one row per screen and phase from /api/assist-timing, loaded when the
  // diagnostics page shows and on Refresh.
  let assistTimingLoaded = false;

  function assistTimingMs(us) {
    return (us / 1000).toFixed(1);
  }

  function assistTimingPhaseName(phase) {
    const key = phase === 'thinking' ? 'assistThinking' : (phase === 'answering' ? 'assistAnswering' : 'assistListening');
    return t(key).replace(/…$/, '');
  }

  function renderAssistTiming(rows, data) {
    const screens = Array.isArray(data?.screens) ? data.screens : [];
    const measured = screens.filter(screen => (screen.phases || []).some(p => p.frames > 0 || p.ticks > 0));
    if (!measured.length) {
      rows.innerHTML = '<tr><td colspan="9">' + escapeHtml(t('assistTimingEmpty')) + '</td></tr>';
      return;
    }
    let html = '';
    measured.forEach(screen => {
      const phases = (screen.phases || []).filter(p => p.frames > 0 || p.ticks > 0);
      phases.forEach((p, index) => {
        html += '<tr>';
        if (index === 0) {
          html += '<td rowspan="' + phases.length + '">' + escapeHtml(screen.id || '') +
            (screen.file ? '' : ' *') +
            (screen.open_us ? '<br><small>' + escapeHtml(assistTimingMs(screen.open_us)) + ' ms</small>' : '') + '</td>';
        }
        html += '<td>' + escapeHtml(assistTimingPhaseName(p.phase)) + '</td>' +
          '<td>' + p.frames + '</td>' +
          '<td>' + assistTimingMs(p.draw_us) + ' / ' + assistTimingMs(p.p95_us) + ' / ' + assistTimingMs(p.max_us) + '</td>' +
          '<td>' + assistTimingMs(p.copy_us) + '</td>' +
          '<td>' + Math.round(p.px / 1000) + 'k</td>' +
          '<td>' + assistTimingMs(p.work_us) + ' / ' + assistTimingMs(p.work_max_us) + '</td>' +
          '<td>' + assistTimingMs(p.gap_us) + ' / ' + assistTimingMs(p.gap_max_us) + '</td>' +
          '<td>' + p.late + '</td>' +
          '</tr>';
      });
    });
    rows.innerHTML = html;
  }

  async function loadAssistTiming() {
    const rows = document.getElementById('assist_timing_rows');
    if (!rows) return;
    try {
      const res = await fetch('/api/assist-timing?ts=' + Date.now());
      if (!res.ok) throw new Error();
      renderAssistTiming(rows, await res.json());
      assistTimingLoaded = true;
    } catch (err) {
      showNotification(t('networkError'), false);
    }
  }

  async function resetAssistTiming() {
    try {
      const res = await fetch('/api/assist-timing/reset', { method: 'POST' });
      if (!res.ok) throw new Error();
    } catch (err) {
      showNotification(t('networkError'), false);
      return;
    }
    loadAssistTiming();
  }
