# Notes

`#define DMA_CHANNEL SPI_DMA_CH_AUTO` is used to automatically select the DMA channel for SPI. T
his is useful when multiple SPI peripherals are used in the same project. The DMA channel is selected 
based on the SPI peripheral used. For example, if SPI1 is used, the DMA channel will be set to 
`SPI_DMA_CH1`. If SPI2 is used, the DMA channel will be set to `SPI_DMA_CH2`, and so on.

On ESP32, the SPI peripheral is selected based on the SPI host used. For example, if SPI host 1 is used,
the SPI peripheral will be set to `HSPI_HOST`. If SPI host 2 is used, the SPI peripheral will be set to
`VSPI_HOST`.

For esp32 it is recommended to use the `SPI_DMA_CH_AUTO` option. This will automatically select the DMA.

USE_HSPI_PORT is used to select the SPI host. Which seems to work.

For Button+ when using DMA we need to swap the bytes.