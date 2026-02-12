import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';
import { CodeIcon, BookIcon, CpuIcon, FileTextIcon, SettingsIcon, FolderIcon, FbLogo } from '../icons/index.js';
import './fb-nav-item.js';

@customElement('fb-navbar')
export class FbNavbar extends LitElement {
  createRenderRoot() { return this; }

  render() {
    return html`
      <nav class="h-screen w-[100px] bg-glass-dark backdrop-blur-md border-r border-border-default overflow-hidden flex flex-col p-6">
        <div class="flex h-full flex-col items-start justify-between gap-6 py-6">
          <!-- Top: Logo + Nav Items -->
          <div class="flex w-full flex-col items-start gap-14">
            <!-- Logo: same placement as Q logo -->
            <div class="flex w-[52px] justify-center">
              ${FbLogo()}
            </div>

            <!-- Main nav items -->
            <div class="flex w-full flex-col gap-4 items-center pl-1.5">
              <fb-nav-item .icon=${CodeIcon()} label="Code" active></fb-nav-item>
              <fb-nav-item .icon=${FolderIcon()} label="Assets" @click=${() => window.dispatchEvent(new CustomEvent('toggle-assets'))}></fb-nav-item>
              <fb-nav-item .icon=${FileTextIcon()} label="Examples"></fb-nav-item>
              <fb-nav-item .icon=${CpuIcon()} label="Device"></fb-nav-item>
              <fb-nav-item .icon=${BookIcon()} label="Docs"></fb-nav-item>
            </div>
          </div>

          <!-- Bottom section -->
          <div class="flex w-full flex-col gap-4 items-center pl-1.5">
            <fb-nav-item .icon=${SettingsIcon()} label="Settings"></fb-nav-item>
          </div>
        </div>
      </nav>
    `;
  }
}
