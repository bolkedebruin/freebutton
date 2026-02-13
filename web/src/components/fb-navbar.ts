import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';
import { CodeIcon, BookIcon, CpuIcon, FileTextIcon, SettingsIcon, FolderIcon, FbLogo } from '../icons/index.js';
import { emit } from '../js/app-state.js';
import './fb-nav-item.js';

@customElement('fb-navbar')
export class FbNavbar extends LitElement {
  createRenderRoot() { return this; }

  render() {
    return html`
      <nav class="h-screen w-[100px] bg-glass-dark backdrop-blur-md border-r border-border-default overflow-hidden flex flex-col pt-6 px-6 pb-0">
        <div class="flex h-full flex-col items-start justify-between gap-6 pt-6 pb-2">
          <!-- Top: Logo + Nav Items -->
          <div class="flex w-full flex-col items-start gap-14">
            <!-- Logo: same placement as Q logo -->
            <div class="flex w-[52px] justify-center">
              ${FbLogo()}
            </div>

            <!-- Main nav items -->
            <div class="flex w-full flex-col gap-4 items-center pl-1.5">
              <fb-nav-item .icon=${CodeIcon()} label="Code" active></fb-nav-item>
              <fb-nav-item .icon=${FolderIcon()} label="Assets" @click=${() => emit('sidebar-panel', { panel: 'assets' })}></fb-nav-item>
              <fb-nav-item .icon=${FileTextIcon()} label="Examples" @click=${() => emit('sidebar-panel', { panel: 'examples' })}></fb-nav-item>
              <fb-nav-item .icon=${CpuIcon()} label="Device"></fb-nav-item>
              <fb-nav-item .icon=${BookIcon()} label="Docs"></fb-nav-item>
            </div>
          </div>

          <!-- Bottom section — pinned to bottom -->
          <div class="flex w-full flex-col items-center pl-1.5 mt-auto">
            <fb-nav-item .icon=${SettingsIcon()} label="Settings"></fb-nav-item>
          </div>
        </div>
      </nav>
    `;
  }
}
